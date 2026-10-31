// =============================================================================
// tcTcpClient.cpp - TCP client socket implementation
// =============================================================================

#include "tc/network/tcTcpClient.h"
#include <stdexcept>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif
#include <deque>
#include <mutex>
#include <thread>
#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"
#include "tc/events/tcCoreEvents.h"
#include <cstring>
#include <algorithm>
#include <climits>
#include <limits>

namespace trussc {
namespace {
thread_local const void* connectingState = nullptr;
thread_local const void* tcpWorkerState = nullptr;

bool setNonBlocking(
#ifdef _WIN32
    SOCKET fd
#else
    int fd
#endif
) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Connect readiness includes socket errors; SO_ERROR supplies the result.
int waitSocket(
#ifdef _WIN32
    SOCKET fd,
#else
    int fd,
#endif
    bool write, int ms) {
#ifdef _WIN32
    fd_set ready, errors;
    FD_ZERO(&ready); FD_SET(fd, &ready);
    FD_ZERO(&errors); FD_SET(fd, &errors);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, write ? nullptr : &ready, write ? &ready : nullptr, &errors, &tv);
#else
    pollfd pfd{fd, static_cast<short>(write ? POLLOUT : POLLIN), 0};
    int result = poll(&pfd, 1, ms);
    return result < 0 && errno == EINTR ? 0 : result;
#endif
}
void finishWaiter(const internal::TcpSendItem& item, SendError error, size_t written) {
    if (!item.waiter) return;
    {
        std::lock_guard<std::mutex> lock(item.waiter->mutex);
        item.waiter->error = error;
        item.waiter->bytesSent = written;
        item.waiter->done = true;
    }
    item.waiter->cv.notify_all();
}

// After a send listener destroyed the client, only the independently owned
// channel remains. Wake any borrowed senders without touching the client.
void cancelQueuedSends(internal::TcpSendChannel& ch) {
    for (;;) {
        internal::TcpSendItem item;
        {
            std::lock_guard<std::mutex> lock(ch.mutex);
            if (ch.queue.empty()) break;
            item = std::move(ch.queue.front());
            ch.queue.pop_front();
            ch.pendingBytes -= item.size;
        }
        finishWaiter(item, SendError::Disconnected, 0);
    }
    ch.room.notify_all();
}
} // namespace


// =============================================================================
// Constructor / Destructor
// =============================================================================
TcpClient::State::~State() {
    keptThreads_.release(connectThread_);
    keptThreads_.release(receiveThread_);
    keptWriters_.release(writerThread_);
}

struct TcpClient::State::ClientSendChannel : internal::TcpSendChannel {
    internal::TcpSendItem active;
    // Cancellation releases payloads/waiters immediately; the writer retains
    // only IDs so completion events still run on its thread.
    std::deque<uint64_t> cancelledSendIds;
    bool hasActive = false;
    bool threaded = false;
    bool draining = false;
    size_t written = 0;
    float timeout = 0;
    std::chrono::steady_clock::time_point progress;
};









// =============================================================================
// Connection management
// =============================================================================
bool TcpClient::State::connect(const std::string& host, int port) {
    if (!prepareConnect()) return false;
    if (connected_ || running_ || connectPending_) {
        disconnectImpl(true, false);
    }

    // Release what is left before starting over.
    //  - After the peer closed the connection (or it failed) the flags above
    //    are all clear, but the socket and the finished receive thread are
    //    still here: overwriting socket_ leaks the descriptor, and assigning
    //    a new thread to a still-joinable receiveThread_ calls std::terminate.
    //  - The disconnect() above fired onDisconnect inline, and a listener may
    //    have reconnected from it. This call came first and overrules that
    //    connection: close it without another notification. running_ is
    //    cleared before the shutdown(), so its receive thread's EOF loses the
    //    exchange in processNetwork() and reports nothing (reported, it would
    //    let the listener reconnect again from that thread while this one is
    //    joining it).
    running_ = false;
    connectPending_ = false;
    updateListener_.disconnect();
    resetConnection();
    if (connected_.exchange(false)) {
        logWarning() << "TcpClient: connect() closes the connection an onDisconnect listener opened";
    }
    // A thread an earlier listener's connect() or disconnect() could not
    // join (it ran on it) has been told to stop: wait for it here, before
    // the new connection starts. The calling thread itself, if kept, stays.
    keptThreads_.joinOthers();

    // This connection's generation, taken before running_ or connected_ is
    // set for it (and before onConnect). A receive thread that a listener's
    // disconnect() let go of is joined above, unless this call runs on it:
    // then it goes back to its loops once the listener returns. Those check
    // the generation together with those flags, and have to see the new
    // generation by the time they can see them set, or the thread reads the
    // new socket next to the new receive thread (or, without threads, next
    // to the update event).
    const unsigned generation = ++receiveGeneration_;
    internal::sharedStore(receiveDeliveryRun_, std::make_shared<std::atomic<bool>>(true));

    if (!transport_->connectSocket(*this, host, port)) return false;
    if (!transport_->isConnecting(*this)) {
        startSendChannel();
        connected_ = true;
        logNotice() << "TCP connected to " << host << ":" << port;
        TcpConnectEventArgs args;
        args.success = true;
        args.message = "Connected";
        onConnect.notify(args);
    }

    if (running_) {
        if (useThread_) {
            // Both reader and writer use non-blocking sockets.
            keptThreads_.start(receiveThread_, [state = shared_from_this(), transport = transport_, generation, alive = alive_] {
                (void)transport; // Retained with the socket throughout this worker.
                state->receiveThreadFunc(generation, alive);
            });
        } else {
            // Register update listener
            updateListener_ = events().update.listen([state = shared_from_this()] { state->processNetwork(); }, EventPriority::BeforeApp);
        }
    }

    return true;
}

bool TcpClient::State::prepareConnect() {
    if (connectingState == this) return !connectCancelled_ && *alive_;
    stopConnectThread();
    connectCancelled_ = false;
    return *alive_;
}

void TcpClient::State::stopConnectThread() {
    connectCancelled_ = true;
    running_ = false;
    connectPending_ = false;
    std::deque<internal::TcpSendItem> cancelled;
    // A connect listener (WebSocket's HTTP request, for example) may be
    // waiting in send(). Cut that wait too before joining the connect worker.
    if (auto ch = sendChannel()) {
        {
            std::lock_guard<std::mutex> lock(ch->mutex);
            ch->open = false;
            // Queued borrowed bytes are not in use. An active item is woken
            // by its writer only after its non-blocking I/O step has ended.
            cancelled.swap(ch->queue);
            for (const auto& item : cancelled) {
                ch->pendingBytes -= item.size;
                ch->cancelledSendIds.push_back(item.id);
            }
        }
        for (const auto& item : cancelled) finishWaiter(item, SendError::Disconnected, 0);
        ch->queued.notify_all();
        ch->room.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socket_ != INVALID_SOCKET) {
#ifdef _WIN32
            ::shutdown(socket_, SD_BOTH);
#else
            ::shutdown(socket_, SHUT_RDWR);
#endif
            CLOSE_SOCKET(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

    // A plain TCP worker waits for none of its peers: a peer listener may be
    // waiting for this callback to return. Their handles stay in the State.
    if (tcpWorkerState != this) {
        keptThreads_.release(connectThread_);
        keptThreads_.joinOthers();
    }
    asyncConnecting_ = false;
}

bool TcpClient::State::connectSocket(const std::string& host, int port) {
    if (connectCancelled_) return false;
    {
        std::lock_guard<std::mutex> lock(targetMutex_);
        remoteHost_ = host;
        remotePort_ = port;
    }
    addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int ret = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (connectCancelled_) {
        if (result) freeaddrinfo(result);
        return false;
    }
    if (ret != 0) {
        notifyError("Failed to resolve host: " + host, ret);
        return false;
    }
    bool nonBlocking = false;
    int error = 0;
    bool created = false;
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (!connectCancelled_) {
            socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            created = socket_ != INVALID_SOCKET;
            if (created) {
                internal::setNoSigpipe(socket_);
                connectStart_ = std::chrono::steady_clock::now();
                nonBlocking = setNonBlocking(socket_);
                ret = nonBlocking ? ::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) : SOCKET_ERROR;
            }
            error = !created || ret == SOCKET_ERROR ? SOCKET_ERROR_CODE : 0;
        }
    }
    if (!created) {
        freeaddrinfo(result);
        if (!connectCancelled_) notifyError("Failed to create socket", error);
        return false;
    }
    freeaddrinfo(result);
    if (!nonBlocking) {
        closeClientSocket();
        notifyError("Failed to make socket non-blocking", error);
        return false;
    }
#ifdef _WIN32
    const bool pending = error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    const bool pending = error == EINPROGRESS;
#endif
    if (ret == SOCKET_ERROR && !pending) {
        closeClientSocket();
        notifyError("Failed to connect to " + host + ":" + std::to_string(port), error);
        return false;
    }
    running_ = true;
    connectPending_ = pending;
    if (useThread_ && pending) {
        int code = 0;
        int state;
        do { state = checkPendingConnect(100, code); } while (state == 0);
        if (state < 0) {
            running_ = false;
            connectPending_ = false;
            closeClientSocket();
            if (state != -2) notifyError("Connection failed or timed out", code);
            return false;
        }
    }
    if (connectCancelled_) {
        running_ = false;
        connectPending_ = false;
        closeClientSocket();
        return false;
    }
    return true;
}

int TcpClient::State::checkPendingConnect(int waitMs, int& error) {
    if (connectCancelled_) return -2;
#ifdef _WIN32
    SOCKET descriptor;
#else
    int descriptor;
#endif
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        descriptor = socket_;
    }
    if (descriptor == INVALID_SOCKET) return -2;
    int ready = waitSocket(descriptor, true, waitMs);
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (connectCancelled_ || socket_ != descriptor) return -2;
    if (ready != 0) {
#ifdef _WIN32
        int len = sizeof(error);
        int result = getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len);
#else
        socklen_t len = sizeof(error);
        int result = getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len);
#endif
        if (result != 0) error = SOCKET_ERROR_CODE;
        if (ready < 0 || error != 0) return -1;
        connectPending_ = false;
        return 1;
    }
    const float timeout = connectTimeout_;
    if (timeout > 0 && std::chrono::steady_clock::now() - connectStart_ >=
            std::chrono::duration<float>(timeout)) {
#ifdef _WIN32
        error = WSAETIMEDOUT;
#else
        error = ETIMEDOUT;
#endif
        return -1;
    }
    return 0;
}

void TcpClient::State::connectAsync(const std::string& host, int port) {
    if (isConnecting()) {
        std::lock_guard<std::mutex> lock(targetMutex_);
        const bool same = asyncConnecting_
            ? asyncHost_ == host && asyncPort_ == port
            : remoteHost_ == host && remotePort_ == port;
        if (same) return;
    }
    // Cancel the old attempt before resetting the connection. Its connect
    // uses non-blocking syscalls and notices cancellation between poll slices.
    disconnect();
    connectCancelled_ = false;
    {
        std::lock_guard<std::mutex> lock(targetMutex_);
        asyncHost_ = host;
        asyncPort_ = port;
    }
    const unsigned attempt = ++connectAttempt_;
    asyncConnecting_ = true; // Published before the worker can start.
    if (useThread_) {
        keptThreads_.start(connectThread_, [state = shared_from_this(), host, port, attempt] {
            state->connectThreadFunc(host, port, attempt);
        });
    } else {
        connectThreadFunc(host, port, attempt);
    }
}

void TcpClient::State::connectThreadFunc(const std::string& host, int port, unsigned attempt) {
    const void* previous = connectingState;
    connectingState = this;
    const void* previousWorker = tcpWorkerState;
    tcpWorkerState = this;
    bool success = !connectCancelled_ && *alive_ && connect(host, port);
    if (connectAttempt_ == attempt) asyncConnecting_ = false;
    if (connectAttempt_ == attempt && !success && !connectCancelled_ && !connected_ && !running_ && !connectPending_) {
        TcpConnectEventArgs args;
        args.success = false;
        args.message = "Connection failed";
        onConnect.notify(args);
    }
    connectingState = previous;
    tcpWorkerState = previousWorker;
}

void TcpClient::State::disconnect() {
    disconnectImpl(true);
}

// disconnect() with notify, the destructor without
void TcpClient::State::disconnectImpl(bool notify, bool stopConnect) {
    if (stopConnect) stopConnectThread();
    running_ = false;
    connectPending_ = false;
    updateListener_.disconnect();

    resetConnection();

    if (tcpWorkerState != this) keptThreads_.joinOthers();

    // The receive thread reports only a close it ran into itself (running_
    // still set). The EOF that the shutdown() above wakes it with is this
    // call's own, and is reported here, once, after the join.
    if (connected_.exchange(false) && notify) {
        TcpDisconnectEventArgs args;
        args.reason = "Disconnected by client";
        args.wasClean = true;
        onDisconnect.notify(args);
    }
}

// Close the socket and release the receive thread (join it, or keep it when
// called on it). connectThread_ is left alone: connect() runs on it for
// connectAsync(), and calls this.
void TcpClient::State::resetConnection() {
    closeClientSocket();
    stopSendChannel();

    // Called from within the receive thread (a listener that disconnects or
    // reconnects), the thread cannot join itself: the client keeps it, and
    // the next connect() or disconnect() on another thread, or the
    // destructor, joins it. Its loops (processNetwork()'s receive loop, then
    // receiveThreadFunc()'s) end on their own once running_ is cleared or a
    // newer receive thread has taken over.
    if (tcpWorkerState != this) keptThreads_.release(receiveThread_);
}

void TcpClient::State::deliverReceive(TcpReceiveEventArgs& args) {
    auto run = internal::sharedLoad(receiveDeliveryRun_);
    onReceive.notifyExternal(args, run);
}

bool TcpClient::State::isConnected() const {
    return connected_;
}

// =============================================================================
// Data transmission
// =============================================================================
bool TcpClient::State::send(const void* data, size_t size) {
    AliveToken alive = alive_;
    auto ch = sendChannel();
    if (!connected_ || !ch) { notifyError("Not connected"); return false; }
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        if (ch->writerId == std::this_thread::get_id() || (!ch->threaded && ch->draining)) return false;
    }
    internal::TcpSendItem item;
    item.borrowed = data;
    item.size = size;
    std::shared_ptr<internal::TcpSendWaiter> waiter;
    try { waiter = std::make_shared<internal::TcpSendWaiter>(); }
    catch (const std::bad_alloc&) { return false; }
    item.waiter = waiter;
    if (!enqueue(std::move(item))) return false;
    if (!ch->threaded) {
        while (*alive) {
            {
                std::lock_guard<std::mutex> held(waiter->mutex);
                if (waiter->done) break;
            }
            drainSendChannel(ch, true, alive);
        }
    }
    if (!*alive && !ch->threaded) { cancelQueuedSends(*ch); return false; }
    if (!ch->threaded && !ch->open) {
        while (drainSendChannel(ch, false, alive)) {
            if (!*alive) { cancelQueuedSends(*ch); return false; }
        }
    }
    std::unique_lock<std::mutex> lock(waiter->mutex);
    waiter->cv.wait(lock, [&] { return waiter->done; });
    return waiter->error == SendError::None;
}

bool TcpClient::State::send(const std::vector<char>& data) {
    return send(data.data(), data.size());
}

bool TcpClient::State::send(const std::string& message) {
    return send(message.data(), message.size());
}

bool TcpClient::State::isConnecting() const {
    return asyncConnecting_ || transport_->isConnecting(*this);
}

std::shared_ptr<TcpClient::State::ClientSendChannel> TcpClient::State::sendChannel() const {
    std::lock_guard<std::mutex> lock(channelMutex_);
    return sendChannel_;
}

void TcpClient::State::startSendChannel() {
    auto ch = std::make_shared<ClientSendChannel>();
    ch->socket = socket_;
    ch->threaded = useThread_;
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        sendChannel_ = ch;
        if (ch->threaded) keptWriters_.start(writerThread_, [state = shared_from_this(), ch, alive = alive_] { state->writerThreadFunc(ch, alive); });
    }
}

void TcpClient::State::stopSendChannel() {
    std::shared_ptr<ClientSendChannel> ch;
    std::thread writer;
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        ch = std::move(sendChannel_);
        if (tcpWorkerState != this) writer = std::move(writerThread_);
    }
    if (ch) {
        {
            std::lock_guard<std::mutex> lock(ch->mutex);
            ch->open = false;
        }
        ch->queued.notify_all();
        ch->room.notify_all();
    }
    keptWriters_.release(writer);
    keptWriters_.joinOthers();
    if (ch && !ch->threaded && !ch->draining) {
        AliveToken alive = alive_;
        while (drainSendChannel(ch, false, alive)) {}
    }
}

void TcpClient::State::waitClientSocket(bool forWrite, int ms) {
#ifdef _WIN32
    SOCKET fd;
#else
    int fd;
#endif
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        fd = socket_;
    }
    // Waiting only observes readiness. Actual recv/send/close operations are
    // serialized separately; holding that lock during a read wait would block
    // the writer even when the send buffer has room.
    if (fd != INVALID_SOCKET) waitSocket(fd, forWrite, ms);
}

void TcpClient::State::closeClientSocket() {
    // Refuse further writes before closing, so cancellation is not reported as
    // a send failure by a writer whose current step sees the closed socket.
    if (auto ch = sendChannel()) {
        { std::lock_guard<std::mutex> lock(ch->mutex); ch->open = false; }
        ch->queued.notify_all();
        ch->room.notify_all();
    }
    {
        // Non-blocking I/O and close share this mutex, preventing descriptor reuse.
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socket_ != INVALID_SOCKET) {
#ifdef _WIN32
            shutdown(socket_, SD_BOTH);
#else
            shutdown(socket_, SHUT_RDWR);
#endif
            CLOSE_SOCKET(socket_);
            socket_ = INVALID_SOCKET;
        }
    }
    // TLS's reset path also relies on closeClientSocket retiring its writer.
    stopSendChannel();
}

int TcpClient::State::writeSendStep(const void* data, size_t size, bool& forWrite, int& error) {
    std::lock_guard<std::mutex> lock(socketMutex_);
    forWrite = true;
    if (socket_ == INVALID_SOCKET) return -2;
    int sent = static_cast<int>(::send(socket_, static_cast<const char*>(data),
                                     static_cast<int>(std::min(size, size_t(INT_MAX))), TC_SEND_FLAGS));
    if (sent >= 0) return sent;
    error = SOCKET_ERROR_CODE;
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINTR ? -1 : -2;
#else
    return error == EWOULDBLOCK || error == EAGAIN || error == EINTR ? -1 : -2;
#endif
}

SendResult TcpClient::State::enqueue(internal::TcpSendItem&& item) {
    auto ch = sendChannel();
    if (!connected_ || !ch) {
        notifyError("Not connected");
        return {SendError::NotRunning, 0};
    }
    std::unique_lock<std::mutex> lock(ch->mutex);
    auto room = [&] {
        size_t mark = sendAsyncBufferSize_;
        return !ch->open || ((mark == 0 || ch->pendingBytes < mark) &&
            item.size <= (std::numeric_limits<size_t>::max)() - ch->pendingBytes);
    };
    if (!room()) {
        if (!item.waiter) {
            lock.unlock();
            notifyError("Send queue full");
            return {SendError::QueueFull, 0};
        }
        if (!ch->threaded) {
            AliveToken alive = alive_;
            while (!room()) {
                lock.unlock();
                drainSendChannel(ch, true, alive);
                if (!*alive) return {SendError::Disconnected, 0};
                lock.lock();
            }
        } else {
            ch->room.wait(lock, room);
        }
    }
    if (!ch->open) {
        lock.unlock();
        notifyError("Connection closed");
        return {SendError::Disconnected, 0};
    }
    item.id = ++nextSendId_;
    const uint64_t id = item.id;
    try { ch->queue.push_back(std::move(item)); }
    catch (const std::bad_alloc&) { return {SendError::AllocationFailed, 0}; }
    ch->pendingBytes += ch->queue.back().size;
    lock.unlock();
    ch->queued.notify_one();
    return {SendError::None, id};
}

SendResult TcpClient::State::sendAsync(const void* data, size_t size) {
    internal::TcpSendItem item;
    try {
        auto bytes = std::make_shared<std::vector<char>>(size);
        if (size) std::memcpy(bytes->data(), data, size);
        item.owned = std::move(bytes);
    } catch (const std::bad_alloc&) {
        return {SendError::AllocationFailed, 0};
    } catch (const std::length_error&) {
        return {SendError::AllocationFailed, 0};
    }
    item.size = size;
    return enqueue(std::move(item));
}

SendResult TcpClient::State::sendAsync(std::vector<char>&& data) {
    internal::TcpSendItem item;
    item.size = data.size();
    try { item.owned = std::make_shared<const std::vector<char>>(std::move(data)); }
    catch (const std::bad_alloc&) { return {SendError::AllocationFailed, 0}; }
    return enqueue(std::move(item));
}

SendResult TcpClient::State::sendAsync(const std::string& message) {
    return sendAsync(message.data(), message.size());
}

bool TcpClient::State::drainSendChannel(const std::shared_ptr<ClientSendChannel>& ch, bool wait,
                                  const AliveToken& alive) {
    if (ch->draining) return false;
    ch->draining = true;
    struct DrainGuard {
        ClientSendChannel& channel;
        ~DrainGuard() { channel.draining = false; }
    } guard{*ch};
    std::deque<uint64_t> cancelledSendIds;
    auto reportCancelled = [&] {
        for (uint64_t id : cancelledSendIds) {
            TcpSendCompleteEventArgs args;
            args.sendId = id;
            args.error = SendError::Disconnected;
            onSendComplete.notify(args);
        }
    };
    if (!ch->hasActive) {
        {
            std::lock_guard<std::mutex> lock(ch->mutex);
            if (ch->queue.empty()) {
                cancelledSendIds.swap(ch->cancelledSendIds);
            } else {
                ch->active = std::move(ch->queue.front());
                ch->queue.pop_front();
                ch->hasActive = true;
                ch->written = 0;
                ch->timeout = *alive ? sendTimeout_.load() : 0.0f;
                ch->progress = std::chrono::steady_clock::now();
            }
        }
        if (!ch->hasActive) {
            reportCancelled();
            return !cancelledSendIds.empty();
        }
    }
    auto& item = ch->active;
    SendError outcome = SendError::None;
    int error = 0;
    while (ch->written < item.size) {
        if (!ch->open || !*alive) { outcome = SendError::Disconnected; break; }
        bool forWrite = true;
        int sent = transport_->writeSendStep(*this,
            static_cast<const char*>(item.data()) + ch->written, item.size - ch->written, forWrite, error);
        if (sent > 0) {
            ch->written += static_cast<size_t>(sent);
            ch->progress = std::chrono::steady_clock::now();
            continue;
        }
        if (sent != -1) { outcome = SendError::Disconnected; break; }
        if (ch->timeout > 0 && std::chrono::steady_clock::now() - ch->progress >=
                std::chrono::duration<float>(ch->timeout)) {
            outcome = SendError::Timeout;
            break;
        }
        // update() never parks; a synchronous caller or the writer waits in
        // cancellable slices. TLS WANT_READ waits for readability instead.
        if (!wait) return false;
        if (ch->open) waitClientSocket(forWrite, 100);
    }
    if (!ch->open && outcome == SendError::None) outcome = SendError::Disconnected;
    const bool failed = outcome != SendError::None && ch->open;
    const bool timedOut = outcome == SendError::Timeout;
    if (timedOut) outcome = SendError::Disconnected; // The stream is closed.
    auto finished = std::move(ch->active);
    const size_t written = ch->written;
    ch->hasActive = false;
    std::deque<internal::TcpSendItem> cancelled;
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        ch->pendingBytes -= finished.size;
        cancelledSendIds.swap(ch->cancelledSendIds);
        if (failed) {
            ch->open = false;
            cancelled.swap(ch->queue);
            for (const auto& item : cancelled) ch->pendingBytes -= item.size;
        }
    }
    if (failed && *alive) {
        running_ = false;
        connected_ = false;
        connectPending_ = false;
    }
    ch->room.notify_all();
    // Release every borrowed sender BEFORE teardown/listeners can join its
    // thread. No payload bytes are read after these notifications.
    finishWaiter(finished, outcome, written);
    for (const auto& item : cancelled) finishWaiter(item, SendError::Disconnected, 0);
    if (failed && *alive) {
        closeClientSocket();
        stopSendChannel();
        notifyError(timedOut ? "Send timed out; client disconnected" : "Send failed", error, !timedOut);
        if (*alive && !connected_ && !running_) {
            TcpDisconnectEventArgs args;
            args.reason = "Send failed";
            args.wasClean = false;
            onDisconnect.notify(args);
        }
    }
    auto report = [&](const internal::TcpSendItem& item, SendError result, size_t bytes) {
        TcpSendCompleteEventArgs args;
        args.sendId = item.id;
        args.error = result;
        args.bytesSent = bytes;
        onSendComplete.notify(args);
    };
    report(finished, outcome, written);
    for (const auto& item : cancelled) report(item, SendError::Disconnected, 0);
    reportCancelled();
    return true;
}

void TcpClient::State::writerThreadFunc(std::shared_ptr<ClientSendChannel> ch, AliveToken alive) {
    tcpWorkerState = this;
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        ch->writerId = std::this_thread::get_id();
    }
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(ch->mutex);
            ch->queued.wait(lock, [&] { return !ch->open || !ch->queue.empty(); });
            if (ch->queue.empty() && ch->cancelledSendIds.empty()) break;
        }
        drainSendChannel(ch, true, alive);
        // A retired run still owes completion for every accepted send. Weak
        // delivery drops these automatically if the handle was destroyed.
    }
}

void TcpClient::State::setSendTimeout(float seconds) {
    sendTimeout_ = seconds > 0 ? seconds : 0;
}
void TcpClient::State::setConnectTimeout(float seconds) {
    connectTimeout_ = seconds > 0 ? seconds : 0;
}
void TcpClient::State::setSendAsyncBufferSize(size_t bytes) {
    sendAsyncBufferSize_ = bytes;
    if (auto ch = sendChannel()) ch->room.notify_all();
}
size_t TcpClient::State::getSendAsyncBufferSize() const { return sendAsyncBufferSize_; }
size_t TcpClient::State::getSendAsyncPendingBytes() const {
    auto ch = sendChannel();
    if (!ch) return 0;
    std::lock_guard<std::mutex> lock(ch->mutex);
    return ch->pendingBytes;
}

// =============================================================================
// Update / Receive logic
// =============================================================================
bool TcpClient::State::processSendQueue(const AliveToken& alive) {
    auto ch = sendChannel();
    if (ch && !running_ && !connected_) {
        // A remote close also retires the idle writer. Leave its handle owned
        // for the next teardown to join; a receive thread must not wait for a
        // writer listener that could be waiting for this receive thread.
        {
            std::lock_guard<std::mutex> lock(ch->mutex);
            ch->open = false;
        }
        ch->queued.notify_all();
        ch->room.notify_all();
    }
    if (ch && !ch->threaded) while (drainSendChannel(ch, false, alive)) {
        if (!*alive) { cancelQueuedSends(*ch); return false; }
    }
    return *alive;
}

void TcpClient::State::processNetwork() {
    // Without threads (driven by the update event) the result is not needed:
    // a stop means the connection ended and the update listener is gone.
    // The token is copied first: a listener may destroy the client.
    AliveToken alive = alive_;
    processNetworkStep(alive);
}

// processNetwork()'s work. Returns false when the caller must stop without
// reading the client again (see the header).
bool TcpClient::State::processNetworkStep(const AliveToken& alive) {
    return transport_->processNetworkStep(*this, alive);
}

bool TcpClient::State::processPlainNetworkStep(const AliveToken& alive) {
    if (!processSendQueue(alive)) return false;
    if (!running_) return true;

    if (connectPending_) {
        int error = 0;
        int state = checkPendingConnect(0, error);
        if (state < 0) {
            disconnect();
            if (state == -2) return false;
            notifyError("Connection failed or timed out", error);
            if (!*alive) return false;
            if (!connected_ && !running_ && !isConnecting()) {
                TcpConnectEventArgs args;
                args.message = "Connection failed";
                onConnect.notify(args);
            }
            return false;
        }
        if (state == 0) return true;
        startSendChannel();
        connected_ = true;
        logNotice() << "TCP connected (async) to " << getRemoteHost() << ":" << getRemotePort();
        TcpConnectEventArgs args;
        args.success = true;
        args.message = "Connected";
        onConnect.notify(args);
        if (!*alive) return false;
    }

    if (!connected_) return true;

    // Receive data. The buffer is this client's own: every client's receive
    // thread runs this at the same time.
    if (recvBuf_.size() != receiveBufferSize_) {
        recvBuf_.resize(receiveBufferSize_);
    }

    // A listener on this thread that reconnects (an onReceive listener that
    // calls connect(), say) sets connected_ again for the new connection,
    // whose own receive thread reads it from then on. The generation stops
    // this loop instead of letting it go back to recv() on the new socket
    // next to that thread, sharing recvBuf_ with it.
    const unsigned generation = receiveGeneration_;
    while (connected_ && receiveGeneration_ == generation) {
        int received, receiveError = 0;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            if (!running_ || socket_ == INVALID_SOCKET) break;
            received = static_cast<int>(recv(socket_, recvBuf_.data(), recvBuf_.size(), 0));
            if (received < 0) receiveError = SOCKET_ERROR_CODE;
        }

        if (received > 0) {
            TcpReceiveEventArgs args;
            args.data.assign(recvBuf_.begin(), recvBuf_.begin() + received);
            deliverReceive(args);
            // An onReceive listener may have destroyed the client (an owner
            // that replaces it from a close it handles inline, say)
            if (!*alive) return false;

            // If using threads, we might block again. 
            // If not, we should return to let the app run.
            if (!useThread_) break; 
        } else if (received == 0) {
            // Connection closed. Report it only if this thread is the one
            // ending the connection. A local disconnect() clears running_
            // before its shutdown() wakes this recv() with EOF, and reports
            // the disconnect itself once it has joined this thread; reporting
            // it here as a remote close would let a reconnecting listener
            // start over while disconnect() is still joining this thread.
            if (running_.exchange(false)) {
                connected_ = false;
                if (!processSendQueue(alive)) return false;
                TcpDisconnectEventArgs args;
                args.reason = "Connection closed by remote";
                args.wasClean = true;
                // This thread stops here, decided before notifying: a
                // listener may destroy, disconnect or reconnect the client,
                // so nothing of it is read after the notification (#262).
                onDisconnect.notifyExternal(args);
                return false;
            }
            break;
        } else {
            // Error
            int err = receiveError;
            if (err == WOULD_BLOCK_ERROR) break;
            
            // As above: an error caused by a local disconnect() is its to report
            if (running_.exchange(false)) {
                connected_ = false;
                if (!processSendQueue(alive)) return false;
                TcpDisconnectEventArgs args;
                args.reason = "Connection error";
                args.wasClean = false;
                // As above: stop without reading the client again
                onDisconnect.notifyExternal(args);
                return false;
            }
            break;
        }
    }
    return true;
}

void TcpClient::State::receiveThreadFunc(unsigned generation, AliveToken alive) {
    tcpWorkerState = this;
    // running_ alone cannot end this loop when a listener on this thread
    // reconnects: connect() lets go of this thread (the client keeps it for
    // a later join), starts the new connection's own, and running_ is true
    // again for that one. The generation says which thread is current
    // (processNetwork()'s receive loop checks it as well).
    // processNetworkStep() returns false once it reported the end of the
    // connection, or a listener destroyed the client: the thread then ends
    // without reading the client again.
    while (running_ && receiveGeneration_ == generation) {
        if (!processNetworkStep(alive)) return;
        if (running_ && receiveGeneration_ == generation) {
            waitClientSocket(false, 100);
        }
    }
}

// =============================================================================
// Settings
// =============================================================================
void TcpClient::State::setReceiveBufferSize(size_t size) {
    receiveBufferSize_ = size;
}

void TcpClient::State::setUseThread(bool use) {
#ifdef __EMSCRIPTEN__
    if (use) {
        logWarning() << "Threads are not supported on Emscripten in this build. useThread remains false.";
        return;
    }
#endif
    if (running_) {
        logWarning() << "Cannot change threading mode while running. Disconnect first.";
        return;
    }
    useThread_ = use;
}

bool TcpClient::State::isUsingThread() const {
    return useThread_;
}

void TcpClient::State::setBlocking(bool blocking) {
    // Every owned socket, including one not yet marked pending/connected,
    // must stay non-blocking so a concurrent setting cannot park connect().
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socket_ == INVALID_SOCKET) return;
        if (!blocking) {
            setNonBlocking(socket_);
            return;
        }
    }
    logWarning() << "TcpClient: active sockets remain non-blocking";
}

// =============================================================================
// Information retrieval
// =============================================================================
std::string TcpClient::State::getRemoteHost() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return remoteHost_;
}

int TcpClient::State::getRemotePort() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return remotePort_;
}

// =============================================================================
// Error notification
// =============================================================================
void TcpClient::State::notifyError(const std::string& msg, int code, bool external) {
    TcpErrorEventArgs args;
    args.message = msg;
    args.errorCode = code;
    if (external) onError.notifyExternal(args);
    else onError.notify(args);
}



void TcpClient::freshState() {
    auto previous = internal::sharedLoad(state_);
    auto next = std::make_shared<State>();
    next->transport_ = previous->transport_;
    next->receiveBufferSize_ = previous->receiveBufferSize_.load();
    next->useThread_ = previous->useThread_.load();
    next->sendTimeout_ = previous->sendTimeout_.load();
    next->connectTimeout_ = previous->connectTimeout_.load();
    next->sendAsyncBufferSize_ = previous->sendAsyncBufferSize_.load();
    next->nextSendId_ = previous->nextSendId_.load();
    next->remoteHost_ = previous->getRemoteHost();
    next->remotePort_ = previous->getRemotePort();
    next->onConnect.target = onConnectTarget_;
    next->onReceive.target = onReceiveTarget_;
    next->onDisconnect.target = onDisconnectTarget_;
    next->onSendComplete.target = onSendCompleteTarget_;
    next->onError.target = onErrorTarget_;
    *previous->alive_ = false;
    retired_.push_back(previous);
    internal::sharedStore(state_, std::move(next));
}
bool TcpClient::Transport::connectSocket(State& state, const std::string& host, int port) {
    return state.connectSocket(host, port);
}
int TcpClient::Transport::writeSendStep(State& state, const void* data, size_t size, bool& forWrite, int& error) {
    return state.writeSendStep(data, size, forWrite, error);
}
bool TcpClient::Transport::isConnecting(const State& state) const { return state.connectPending_; }
bool TcpClient::Transport::processNetworkStep(State& state, const std::shared_ptr<std::atomic<bool>>& alive) {
    return state.processPlainNetworkStep(alive);
}
void TcpClient::setTransport(std::shared_ptr<Transport> transport) {
    internal::sharedLoad(state_)->transport_ = std::move(transport);
}
TcpClient::TcpClient(TcpClient&& other) noexcept : TcpClient() { *this = std::move(other); }
TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this == &other) return *this;
    auto alive = alive_;
    auto sourceAlive = other.alive_;
    disconnect();
    if (!*alive || !*sourceAlive) return *this;
    if (internal::sharedLoad(other.state_)->running_ || internal::sharedLoad(other.state_)->connected_ || internal::sharedLoad(other.state_)->isConnecting() ||
        (internal::sharedLoad(other.state_).use_count() > 2 && internal::sharedLoad(other.state_)->receiveThread_.joinable())) {
        logError("TcpClient") << "moved while running; stopped";
        if (!*sourceAlive) return *this;
        other.disconnect();
        if (!*sourceAlive) return *this;
        // A local disconnect listener may have opened another connection.
        // Moving still transfers only a stopped State, without notifying twice.
        auto replacement = internal::sharedLoad(other.state_);
        if (replacement->running_ || replacement->connected_ || replacement->isConnecting()) {
            replacement->disconnectImpl(false);
            other.freshState();
            other.joinRetired();
        }
    } else {
        internal::sharedLoad(other.state_)->disconnectImpl(false);
        other.joinRetired();
    }
    internal::sharedStore(state_, internal::sharedLoad(other.state_));
    internal::sharedLoad(state_)->onConnect.target = onConnectTarget_;
    internal::sharedLoad(state_)->onReceive.target = onReceiveTarget_;
    internal::sharedLoad(state_)->onDisconnect.target = onDisconnectTarget_;
    internal::sharedLoad(state_)->onSendComplete.target = onSendCompleteTarget_;
    internal::sharedLoad(state_)->onError.target = onErrorTarget_;
    internal::sharedStore(other.state_, std::make_shared<State>());
    other.freshState();
    return *this;
}
void TcpClient::notifyError(const std::string& msg, int code) {
    auto state = internal::sharedLoad(state_);
    state->notifyError(msg, code);
}


void TcpClient::joinRetired() {
    if (tcpWorkerState) {
        // A completed run has no worker owners left. Reap it even from a new
        // run's callback, but never wait for an in-flight receive callback.
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (it->use_count() == 1) {
                (*it)->disconnectImpl(false);
                it = retired_.erase(it);
            } else ++it;
        }
        return;
    }
    auto retired = std::move(retired_);
    for (auto& state : retired) {
        state->disconnectImpl(false);
    }
}

void TcpClient::attachTargets() {
    onConnectTarget_ = internal::EventSnapshotDelivery<TcpConnectEventArgs>::bind(onConnect, delivery_);
    internal::sharedLoad(state_)->onConnect.target = onConnectTarget_;
    onReceiveTarget_ = internal::EventSnapshotDelivery<TcpReceiveEventArgs>::bind(onReceive, delivery_);
    internal::sharedLoad(state_)->onReceive.target = onReceiveTarget_;
    onDisconnectTarget_ = internal::EventSnapshotDelivery<TcpDisconnectEventArgs>::bind(onDisconnect, delivery_);
    internal::sharedLoad(state_)->onDisconnect.target = onDisconnectTarget_;
    onSendCompleteTarget_ = internal::EventSnapshotDelivery<TcpSendCompleteEventArgs>::bind(onSendComplete, delivery_);
    internal::sharedLoad(state_)->onSendComplete.target = onSendCompleteTarget_;
    onErrorTarget_ = internal::EventSnapshotDelivery<TcpErrorEventArgs>::bind(onError, delivery_);
    internal::sharedLoad(state_)->onError.target = onErrorTarget_;
}
TcpClient::TcpClient() { internal::ensureWinsock(); attachTargets(); }
TcpClient::~TcpClient() {
    delivery_->close();
    onConnect.clear();
    onReceive.clear();
    onDisconnect.clear();
    onSendComplete.clear();
    onError.clear();

    onConnectTarget_.reset();
    onReceiveTarget_.reset();
    onDisconnectTarget_.reset();
    onSendCompleteTarget_.reset();
    onErrorTarget_.reset();
    *alive_ = false;
    auto state = internal::sharedLoad(state_);
    *state->alive_ = false;
    state->disconnectImpl(false);
    joinRetired();
}
bool TcpClient::connect(const std::string& host, int port) {
    auto alive = alive_;
    if (connectingState != internal::sharedLoad(state_).get()) {
        disconnect();
        if (!*alive) return false;
        if (internal::sharedLoad(state_)->running_ || internal::sharedLoad(state_)->connected_ || internal::sharedLoad(state_)->isConnecting()) {
            auto superseded = internal::sharedLoad(state_);
            superseded->disconnectImpl(false);
            if (!*alive) return false;
            freshState();
            joinRetired();
            if (!*alive) return false;
            logWarning() << "TcpClient: connect() closes the connection an onDisconnect listener opened";
        }
    }
    remoteHost_ = host;
    remotePort_ = port;
    auto state = internal::sharedLoad(state_);
    return state->connect(host, port);
}
bool TcpClient::prepareConnect() {
    auto state = internal::sharedLoad(state_);
    return state->prepareConnect();
}
void TcpClient::stopConnectThread() {
    auto state = internal::sharedLoad(state_);
    state->stopConnectThread();
}
bool TcpClient::connectSocket(const std::string& host, int port) {
    remoteHost_ = host;
    remotePort_ = port;
    auto state = internal::sharedLoad(state_);
    return state->connectSocket(host, port);
}
int TcpClient::checkPendingConnect(int waitMs, int& error) {
    auto state = internal::sharedLoad(state_);
    return state->checkPendingConnect(waitMs, error);
}
void TcpClient::connectAsync(const std::string& host, int port) {
    auto alive = alive_;
    auto current = internal::sharedLoad(state_);
    if (current->isConnecting()) {
        std::lock_guard<std::mutex> lock(current->targetMutex_);
        const bool same = current->asyncConnecting_
            ? current->asyncHost_ == host && current->asyncPort_ == port
            : current->remoteHost_ == host && current->remotePort_ == port;
        if (same) return;
    }
    disconnect();
    if (!*alive) return;
    auto state = internal::sharedLoad(state_);
    state->connectAsync(host, port);
}
void TcpClient::disconnect() {
    auto state = internal::sharedLoad(state_);
    auto alive = alive_;
    state->disconnect();
    if (!*alive) return;
    if (internal::sharedLoad(state_) == state) freshState();
    joinRetired();
}
bool TcpClient::isConnected() const {
    auto state = internal::sharedLoad(state_);
    return state->isConnected();
}
bool TcpClient::send(const void* data, size_t size) {
    auto state = internal::sharedLoad(state_);
    return state->send(data, size);
}
bool TcpClient::send(const std::vector<char>& data) {
    auto state = internal::sharedLoad(state_);
    return state->send(data);
}
bool TcpClient::send(const std::string& message) {
    auto state = internal::sharedLoad(state_);
    return state->send(message);
}
bool TcpClient::isConnecting() const {
    auto state = internal::sharedLoad(state_);
    return state->isConnecting();
}
void TcpClient::startSendChannel() {
    auto state = internal::sharedLoad(state_);
    state->startSendChannel();
}
void TcpClient::stopSendChannel() {
    auto state = internal::sharedLoad(state_);
    state->stopSendChannel();
}
void TcpClient::waitClientSocket(bool forWrite, int ms) {
    auto state = internal::sharedLoad(state_);
    state->waitClientSocket(forWrite, ms);
}
void TcpClient::closeClientSocket() {
    auto state = internal::sharedLoad(state_);
    state->closeClientSocket();
}
int TcpClient::writeSendStep(const void* data, size_t size, bool& forWrite, int& error) {
    auto state = internal::sharedLoad(state_);
    return state->writeSendStep(data, size, forWrite, error);
}
SendResult TcpClient::sendAsync(const void* data, size_t size) {
    auto state = internal::sharedLoad(state_);
    return state->sendAsync(data, size);
}
SendResult TcpClient::sendAsync(std::vector<char>&& data) {
    auto state = internal::sharedLoad(state_);
    return state->sendAsync(std::move(data));
}
SendResult TcpClient::sendAsync(const std::string& message) {
    auto state = internal::sharedLoad(state_);
    return state->sendAsync(message);
}
void TcpClient::setSendTimeout(float seconds) {
    auto state = internal::sharedLoad(state_);
    state->setSendTimeout(seconds);
}
void TcpClient::setConnectTimeout(float seconds) {
    auto state = internal::sharedLoad(state_);
    state->setConnectTimeout(seconds);
}
void TcpClient::setSendAsyncBufferSize(size_t bytes) {
    auto state = internal::sharedLoad(state_);
    state->setSendAsyncBufferSize(bytes);
}
size_t TcpClient::getSendAsyncBufferSize() const {
    auto state = internal::sharedLoad(state_);
    return state->getSendAsyncBufferSize();
}
size_t TcpClient::getSendAsyncPendingBytes() const {
    auto state = internal::sharedLoad(state_);
    return state->getSendAsyncPendingBytes();
}
bool TcpClient::processSendQueue(const AliveToken& alive) {
    auto state = internal::sharedLoad(state_);
    return state->processSendQueue(alive);
}
bool TcpClient::processNetworkStep(const AliveToken& alive) {
    auto state = internal::sharedLoad(state_);
    return state->processNetworkStep(alive);
}
void TcpClient::processNetwork() {
    auto state = internal::sharedLoad(state_);
    state->processNetwork();
}
void TcpClient::setReceiveBufferSize(size_t size) {
    auto state = internal::sharedLoad(state_);
    state->setReceiveBufferSize(size);
}
void TcpClient::setUseThread(bool use) {
    auto state = internal::sharedLoad(state_);
    state->setUseThread(use);
}
bool TcpClient::isUsingThread() const {
    auto state = internal::sharedLoad(state_);
    return state->isUsingThread();
}
void TcpClient::setBlocking(bool blocking) {
    auto state = internal::sharedLoad(state_);
    state->setBlocking(blocking);
}
std::string TcpClient::getRemoteHost() const {
    auto state = internal::sharedLoad(state_);
    return state->getRemoteHost();
}
int TcpClient::getRemotePort() const {
    auto state = internal::sharedLoad(state_);
    return state->getRemotePort();
}

} // namespace trussc
