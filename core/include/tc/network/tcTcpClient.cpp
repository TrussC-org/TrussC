// =============================================================================
// tcTcpClient.cpp - TCP client socket implementation
// =============================================================================

#include "tc/network/tcTcpClient.h"
#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"
#include "tc/events/tcCoreEvents.h"
#include <cstring>
#include <algorithm>
#include <climits>

namespace trussc {
namespace {
thread_local const TcpClient* connectingClient = nullptr;

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
struct TcpClient::ClientSendChannel : internal::TcpSendChannel {
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

TcpClient::TcpClient() {
    internal::ensureWinsock();
#ifdef __EMSCRIPTEN__
    useThread_ = false;
#endif
}

TcpClient::~TcpClient() {
    // A receive thread whose listener is destroying this client checks this
    // once the notification returns, and stops without reading the client
    *alive_ = false;
    // Disconnect without onDisconnect: a listener that reconnects would
    // reconnect a client that is going away.
    disconnectImpl(false);
}

TcpClient::TcpClient(TcpClient&& other) noexcept
    : socket_(other.socket_)
    , remoteHost_(std::move(other.remoteHost_))
    , remotePort_(other.remotePort_)
    , running_(other.running_.load())
    , connected_(other.connected_.load())
    , receiveBufferSize_(other.receiveBufferSize_.load())
{
    // recvBuf_ is not taken from `other`: it is scratch space that
    // processNetwork() sizes on the next receive, and a receive thread of
    // `other` may still be reading into it.
#ifdef _WIN32
    other.socket_ = INVALID_SOCKET;
#else
    other.socket_ = -1;
#endif
    other.running_ = false;
    other.connected_ = false;
}

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        socket_ = other.socket_;
        remoteHost_ = std::move(other.remoteHost_);
        remotePort_ = other.remotePort_;
        running_ = other.running_.load();
        connected_ = other.connected_.load();
        receiveBufferSize_ = other.receiveBufferSize_.load();
        // recvBuf_ stays this object's own (see the move constructor).

#ifdef _WIN32
        other.socket_ = INVALID_SOCKET;
#else
        other.socket_ = -1;
#endif
        other.running_ = false;
        other.connected_ = false;
    }
    return *this;
}

// =============================================================================
// Connection management
// =============================================================================
bool TcpClient::connect(const std::string& host, int port) {
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

    if (!connectSocket(host, port)) return false;
    if (!connectPending_) {
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
            keptThreads_.start(receiveThread_, [this, generation, alive = alive_] {
                receiveThreadFunc(generation, alive);
            });
        } else {
            // Register update listener
            updateListener_ = events().update.listen(this, &TcpClient::processNetwork);
        }
    }

    return true;
}

bool TcpClient::prepareConnect() {
    if (connectingClient == this) return !connectCancelled_ && *alive_;
    stopConnectThread();
    connectCancelled_ = false;
    return *alive_;
}

void TcpClient::stopConnectThread() {
    connectCancelled_ = true;
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
    keptThreads_.release(connectThread_);
    keptThreads_.joinOthers();
    asyncConnecting_ = false;
}

bool TcpClient::connectSocket(const std::string& host, int port) {
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
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (socket_ == INVALID_SOCKET) {
        freeaddrinfo(result);
        notifyError("Failed to create socket", SOCKET_ERROR_CODE);
        return false;
    }
    internal::setNoSigpipe(socket_);
    connectStart_ = std::chrono::steady_clock::now();
    bool nonBlocking;
    int error;
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        nonBlocking = setNonBlocking(socket_);
        ret = nonBlocking ? ::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) : SOCKET_ERROR;
        error = ret == SOCKET_ERROR ? SOCKET_ERROR_CODE : 0;
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

int TcpClient::checkPendingConnect(int waitMs, int& error) {
    if (connectCancelled_) return -2;
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (socket_ == INVALID_SOCKET) return -2;
    int ready = waitSocket(socket_, true, waitMs);
    if (connectCancelled_) return -2;
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

void TcpClient::connectAsync(const std::string& host, int port) {
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
        keptThreads_.start(connectThread_, [this, host, port, attempt] {
            connectThreadFunc(host, port, attempt);
        });
    } else {
        connectThreadFunc(host, port, attempt);
    }
}

void TcpClient::connectThreadFunc(const std::string& host, int port, unsigned attempt) {
    const TcpClient* previous = connectingClient;
    connectingClient = this;
    bool success = !connectCancelled_ && *alive_ && connect(host, port);
    if (connectAttempt_ == attempt) asyncConnecting_ = false;
    if (connectAttempt_ == attempt && !success && !connectCancelled_ && !connected_ && !running_ && !connectPending_) {
        TcpConnectEventArgs args;
        args.success = false;
        args.message = "Connection failed";
        onConnect.notify(args);
    }
    connectingClient = previous;
}

void TcpClient::disconnect() {
    disconnectImpl(true);
}

// disconnect() with notify, the destructor without
void TcpClient::disconnectImpl(bool notify, bool stopConnect) {
    if (stopConnect) stopConnectThread();
    running_ = false;
    connectPending_ = false;
    updateListener_.disconnect();

    resetConnection();

    keptThreads_.joinOthers();

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
void TcpClient::resetConnection() {
    closeClientSocket();

    // Called from within the receive thread (a listener that disconnects or
    // reconnects), the thread cannot join itself: the client keeps it, and
    // the next connect() or disconnect() on another thread, or the
    // destructor, joins it. Its loops (processNetwork()'s receive loop, then
    // receiveThreadFunc()'s) end on their own once running_ is cleared or a
    // newer receive thread has taken over.
    keptThreads_.release(receiveThread_);
}

bool TcpClient::isConnected() const {
    return connected_;
}

// =============================================================================
// Data transmission
// =============================================================================
bool TcpClient::send(const void* data, size_t size) {
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
    auto waiter = std::make_shared<internal::TcpSendWaiter>();
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

bool TcpClient::send(const std::vector<char>& data) {
    return send(data.data(), data.size());
}

bool TcpClient::send(const std::string& message) {
    return send(message.data(), message.size());
}

bool TcpClient::isConnecting() const {
    return asyncConnecting_ || connectPending_;
}

std::shared_ptr<TcpClient::ClientSendChannel> TcpClient::sendChannel() const {
    std::lock_guard<std::mutex> lock(channelMutex_);
    return sendChannel_;
}

void TcpClient::startSendChannel() {
    auto ch = std::make_shared<ClientSendChannel>();
    ch->socket = socket_;
    ch->threaded = useThread_;
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        sendChannel_ = ch;
        if (ch->threaded) writerThread_ = std::thread(&TcpClient::writerThreadFunc, this, ch, alive_);
    }
}

void TcpClient::stopSendChannel() {
    std::shared_ptr<ClientSendChannel> ch;
    std::thread writer;
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        ch = std::move(sendChannel_);
        writer = std::move(writerThread_);
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

void TcpClient::waitClientSocket(bool forWrite, int ms) {
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

void TcpClient::closeClientSocket() {
    stopSendChannel();
    // Socket I/O and close use this lock and non-blocking sockets. Taking and resetting
    // the handle under it makes simultaneous TLS/main teardown close only once
    // and prevents a receive/write step using a recycled descriptor.
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

int TcpClient::writeSendStep(const void* data, size_t size, bool& forWrite, int& error) {
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

SendResult TcpClient::enqueue(internal::TcpSendItem&& item) {
    auto ch = sendChannel();
    if (!connected_ || !ch) return {SendError::NotRunning, 0};
    std::unique_lock<std::mutex> lock(ch->mutex);
    auto room = [&] {
        size_t mark = sendAsyncBufferSize_;
        return !ch->open || mark == 0 || ch->pendingBytes < mark;
    };
    if (!room()) {
        if (!item.waiter) return {SendError::QueueFull, 0};
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
    if (!ch->open) return {SendError::Disconnected, 0};
    item.id = ++nextSendId_;
    const uint64_t id = item.id;
    ch->queue.push_back(std::move(item));
    ch->pendingBytes += ch->queue.back().size;
    lock.unlock();
    ch->queued.notify_one();
    return {SendError::None, id};
}

SendResult TcpClient::sendAsync(const void* data, size_t size) {
    internal::TcpSendItem item;
    auto bytes = std::make_shared<std::vector<char>>(size);
    if (size) std::memcpy(bytes->data(), data, size);
    item.owned = std::move(bytes);
    item.size = size;
    return enqueue(std::move(item));
}

SendResult TcpClient::sendAsync(std::vector<char>&& data) {
    internal::TcpSendItem item;
    item.size = data.size();
    item.owned = std::make_shared<const std::vector<char>>(std::move(data));
    return enqueue(std::move(item));
}

SendResult TcpClient::sendAsync(const std::string& message) {
    return sendAsync(message.data(), message.size());
}

bool TcpClient::drainSendChannel(const std::shared_ptr<ClientSendChannel>& ch, bool wait,
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
            if (!*alive) break;
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
        int sent = writeSendStep(static_cast<const char*>(item.data()) + ch->written,
                                 item.size - ch->written, forWrite, error);
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
        notifyError(timedOut ? "Send timed out; client disconnected" : "Send failed", error);
        if (*alive && !connected_ && !running_) {
            TcpDisconnectEventArgs args;
            args.reason = "Send failed";
            args.wasClean = false;
            onDisconnect.notify(args);
        }
    }
    auto report = [&](const internal::TcpSendItem& item, SendError result, size_t bytes) {
        if (!*alive) return;
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

void TcpClient::writerThreadFunc(std::shared_ptr<ClientSendChannel> ch, AliveToken alive) {
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
        if (!*alive) { cancelQueuedSends(*ch); return; }
    }
}

void TcpClient::setSendTimeout(float seconds) {
    sendTimeout_ = seconds > 0 ? seconds : 0;
}
void TcpClient::setConnectTimeout(float seconds) {
    connectTimeout_ = seconds > 0 ? seconds : 0;
}
void TcpClient::setSendAsyncBufferSize(size_t bytes) {
    sendAsyncBufferSize_ = bytes;
    if (auto ch = sendChannel()) ch->room.notify_all();
}
size_t TcpClient::getSendAsyncBufferSize() const { return sendAsyncBufferSize_; }
size_t TcpClient::getSendAsyncPendingBytes() const {
    auto ch = sendChannel();
    if (!ch) return 0;
    std::lock_guard<std::mutex> lock(ch->mutex);
    return ch->pendingBytes;
}

// =============================================================================
// Update / Receive logic
// =============================================================================
bool TcpClient::processSendQueue(const AliveToken& alive) {
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

void TcpClient::processNetwork() {
    // Without threads (driven by the update event) the result is not needed:
    // a stop means the connection ended and the update listener is gone.
    // The token is copied first: a listener may destroy the client.
    AliveToken alive = alive_;
    processNetworkStep(alive);
}

// processNetwork()'s work. Returns false when the caller must stop without
// reading the client again (see the header).
bool TcpClient::processNetworkStep(const AliveToken& alive) {
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
            onReceive.notify(args);
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
                onDisconnect.notify(args);
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
                onDisconnect.notify(args);
                return false;
            }
            break;
        }
    }
    return true;
}

void TcpClient::receiveThreadFunc(unsigned generation, AliveToken alive) {
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
void TcpClient::setReceiveBufferSize(size_t size) {
    receiveBufferSize_ = size;
}

void TcpClient::setUseThread(bool use) {
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

bool TcpClient::isUsingThread() const {
    return useThread_;
}

void TcpClient::setBlocking(bool blocking) {
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
std::string TcpClient::getRemoteHost() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return remoteHost_;
}

int TcpClient::getRemotePort() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return remotePort_;
}

// =============================================================================
// Error notification
// =============================================================================
void TcpClient::notifyError(const std::string& msg, int code) {
    TcpErrorEventArgs args;
    args.message = msg;
    args.errorCode = code;
    onError.notify(args);
}

} // namespace trussc
