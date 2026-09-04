// =============================================================================
// tcTcpServer.cpp - TCP server socket implementation
// =============================================================================

#include "tc/network/tcTcpServer.h"
#include "tc/utils/tcLog.h"
#include <cstring>

#ifdef _WIN32
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#ifndef _WIN32
    #include <poll.h>       // poll(), for the send/receive waits below
#endif
#include <chrono>

// Writing to a socket the peer already closed raises SIGPIPE, whose default
// action terminates the process. MSG_NOSIGNAL suppresses it per call, and both
// Linux and current Apple SDKs define it. Older Apple SDKs do not, so each
// accepted socket also gets SO_NOSIGPIPE below — either mechanism alone is
// enough (verified on macOS 26.5 with a four-way probe: unprotected sends die
// on signal 13, each option alone survives). Windows has no SIGPIPE at all and
// does not define MSG_NOSIGNAL, so the flag is 0 there.
#if defined(MSG_NOSIGNAL)
    #define TC_SEND_FLAGS MSG_NOSIGNAL
#else
    #define TC_SEND_FLAGS 0
#endif

namespace trussc {

std::atomic<int> TcpServer::instanceCount_{0};

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
#else
using socket_t = int;
#endif

// Accepted sockets are non-blocking, and both the send loop and the receive
// thread wait in slices instead of parking in the kernel. A blocking call
// cannot be interrupted portably: Winsock's shutdown() does not wake a send()
// that is already parked, so disconnecting a peer that stopped reading hung
// until that peer moved. Waiting in slices lets either loop notice the channel
// closing on its own, which is the same on every platform.
bool setNonBlocking(socket_t s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Wait for the socket to become writable (forWrite) or readable, up to
// sliceMs. Returns 1 ready, 0 timed out, -1 socket error.
int waitReady(socket_t s, bool forWrite, int sliceMs) {
#ifdef _WIN32
    fd_set fds, exceptfds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    FD_ZERO(&exceptfds);
    FD_SET(s, &exceptfds);
    struct timeval tv;
    tv.tv_sec = sliceMs / 1000;
    tv.tv_usec = (sliceMs % 1000) * 1000;
    int res = ::select(0, forWrite ? NULL : &fds, forWrite ? &fds : NULL, &exceptfds, &tv);
    if (res <= 0) return res == 0 ? 0 : -1;
    return FD_ISSET(s, &exceptfds) ? -1 : 1;
#else
    struct pollfd pfd;
    pfd.fd = s;
    pfd.events = static_cast<short>(forWrite ? POLLOUT : POLLIN);
    pfd.revents = 0;
    int res = ::poll(&pfd, 1, sliceMs);
    if (res < 0) return errno == EINTR ? 0 : -1;
    if (res == 0) return 0;
    if (pfd.revents & (POLLERR | POLLNVAL)) return -1;
    return 1;
#endif
}

// The error a failed wait left on the socket. poll() reports POLLERR without
// touching errno, so read it from the socket rather than guessing.
int socketError(socket_t s) {
    int err = 0;
#ifdef _WIN32
    int len = static_cast<int>(sizeof(err));
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len) != 0) return SOCKET_ERROR_CODE;
#else
    socklen_t len = sizeof(err);
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return SOCKET_ERROR_CODE;
#endif
    return err;
}

// How long a wait parks before re-checking whether the channel is still open.
//
// A longer value was tried, on the theory that closeChannel() shuts the socket
// down before closing it and a shutdown socket is reported ready at once, so the
// timeout would never be what ends a wait. Measured across CI at 10 s:
//
//   Linux    disconnectClient  5 ms    shutdown() wakes poll()
//   macOS    disconnectClient 35 ms    shutdown() wakes poll()
//   Windows  disconnectClient  4+ s    shutdown() does NOT wake select()
//
// So Winsock declines to wake a waiting select() for the same reason it declines
// to wake a parked send(), and on Windows this slice is not a backstop at all —
// it is the mechanism. macOS additionally hung a full slice per round in the
// teardown stress case, which points at a receive thread polling a descriptor
// closed and recycled under it between its `open` check and its wait; the short
// slice bounds that too. Both want fixing before this number can grow.
constexpr int kWaitSliceMs = 100;

} // namespace

// =============================================================================
// Winsock initialization (Windows only)
// =============================================================================
void TcpServer::initWinsock() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            logError() << "Winsock initialization failed";
        }
        initialized = true;
    }
#endif
}

void TcpServer::cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// =============================================================================
// Constructor / Destructor
// =============================================================================
TcpServer::TcpServer() {
    if (instanceCount_++ == 0) {
        initWinsock();
    }
}

TcpServer::~TcpServer() {
    stop();
    if (--instanceCount_ == 0) {
        cleanupWinsock();
    }
}

// =============================================================================
// Server management
// =============================================================================
bool TcpServer::start(int port, int maxClients) {
    if (running_) {
        stop();
    }

    port_ = port;
    maxClients_ = maxClients;

    // Create socket
    serverSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (serverSocket_ == INVALID_SOCKET) {
#else
    if (serverSocket_ < 0) {
#endif
        notifyError("Failed to create server socket", SOCKET_ERROR_CODE);
        return false;
    }

    // Set SO_REUSEADDR option (to allow immediate port reuse on restart)
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (::bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        notifyError("Failed to bind server socket to port " + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(serverSocket_);
#ifdef _WIN32
        serverSocket_ = INVALID_SOCKET;
#else
        serverSocket_ = -1;
#endif
        return false;
    }

    // Start listening
    if (::listen(serverSocket_, maxClients) == SOCKET_ERROR) {
        notifyError("Failed to listen on port " + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(serverSocket_);
#ifdef _WIN32
        serverSocket_ = INVALID_SOCKET;
#else
        serverSocket_ = -1;
#endif
        return false;
    }

    running_ = true;
    acceptThread_ = std::thread(&TcpServer::acceptThreadFunc, this);

    logNotice() << "TCP server started on port " << port;
    return true;
}

void TcpServer::stop() {
    // A server that never ran (or was already stopped) cleans up silently —
    // logging "stopped" from e.g. a static instance's destructor in a CLI
    // that never called start() would be announcing an event that didn't
    // happen. Cleanup below is idempotent either way.
    bool wasRunning = running_;
    running_ = false;

    // Close server socket (unblocks accept)
#ifdef _WIN32
    if (serverSocket_ != INVALID_SOCKET) {
        shutdown(serverSocket_, SD_BOTH);
        CLOSE_SOCKET(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }
#else
    if (serverSocket_ >= 0) {
        shutdown(serverSocket_, SHUT_RDWR);  // Required on Linux to unblock accept()
        CLOSE_SOCKET(serverSocket_);
        serverSocket_ = -1;
    }
#endif

    // Wait for accept thread
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // Disconnect all clients
    disconnectAllClients();

    if (wasRunning) logNotice() << "TCP server stopped";
}

bool TcpServer::isRunning() const {
    return running_;
}

// =============================================================================
// Accept thread
// =============================================================================
void TcpServer::acceptThreadFunc() {
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);

#ifdef _WIN32
        SOCKET clientSocket = ::accept(serverSocket_, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
#else
        int clientSocket = ::accept(serverSocket_, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket < 0) {
#endif
            if (running_) {
                // Only notify error if occurred while running
                // (ignore errors during server shutdown)
            }
            continue;
        }

        // Get client information
        char hostStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
        int clientPort = ntohs(clientAddr.sin_port);

#ifdef SO_NOSIGPIPE
        // Belt and braces for Apple SDKs that predate MSG_NOSIGNAL
        {
            int on = 1;
            ::setsockopt(clientSocket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        }
#endif

        // SO_SNDTIMEO would bound a blocking send, but nothing can then cut
        // that send short when the client is disconnected. The send loop polls
        // and times out on its own instead.
        if (!setNonBlocking(clientSocket)) {
            logWarning() << "Client socket could not be made non-blocking; "
                            "sends to it will not observe disconnects promptly";
        }

        // Register client
        int clientId;
        {
            std::unique_lock<std::shared_mutex> lock(clientsMutex_);
            clientId = nextClientId_++;
            TcpServerClient client;
            client.id_ = clientId;
            client.host_ = hostStr;
            client.port_ = clientPort;
            client.socket_ = clientSocket;
            client.channel_ = std::make_shared<internal::TcpSendChannel>();
            client.channel_->socket = clientSocket;
            clients_[clientId] = client;
        }

        // Start the writer thread before announcing the client: a listener on
        // onClientConnect may well send something, and there has to be
        // somebody to drain it.
        {
            std::unique_lock<std::shared_mutex> lock(clientsMutex_);
            auto it = clients_.find(clientId);
            if (it != clients_.end()) {
                clientWriters_[clientId] =
                    std::thread(&TcpServer::writerThreadFunc, this, clientId, it->second.channel_);
            }
        }

        logNotice() << "Client " << clientId << " connected from " << hostStr << ":" << clientPort;

        // Notify connection event
        TcpClientConnectEventArgs args;
        args.clientId = clientId;
        args.host = hostStr;
        args.port = clientPort;
        onClientConnect.notify(args);

        // Start receive thread for client
        {
            std::unique_lock<std::shared_mutex> lock(clientsMutex_);
            clientThreads_[clientId] = std::thread(&TcpServer::clientThreadFunc, this, clientId);
        }
    }
}

// =============================================================================
// Client receive thread
// =============================================================================
void TcpServer::clientThreadFunc(int clientId) {
    std::vector<char> buffer(receiveBufferSize_);

#ifdef _WIN32
    SOCKET clientSocket;
#else
    int clientSocket;
#endif

    {
        std::shared_lock<std::shared_mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return;
        clientSocket = it->second.socket_;
    }

    // Holding the channel keeps this loop's view of the socket's lifetime in
    // step with the send path: `open` is cleared before the descriptor is
    // closed, so checking it first keeps this thread off a closed (or already
    // recycled) descriptor.
    std::shared_ptr<internal::TcpSendChannel> channel = findChannel(clientId);

    while (running_) {
        if (channel && !channel->open) break;

        // The send path gave up on this client (a timeout truncated a payload).
        // The removal happens here because the writer thread cannot tear down
        // the client it is running for — it would end up joining itself.
        if (channel && channel->dropped) {
            removeClient(clientId);
            break;
        }

        // The socket is non-blocking, so wait in slices rather than parking in
        // recv(): this thread then notices the server stopping and the client
        // being disconnected without waiting for traffic that may never come.
        const int ready = waitReady(clientSocket, false, kWaitSliceMs);
        if (ready == 0) continue;
        if (ready < 0) {
            if (running_ && channel && channel->open) {
                TcpClientDisconnectEventArgs args;
                args.clientId = clientId;
                args.reason = "Connection error";
                args.wasClean = false;
                onClientDisconnect.notify(args);

                removeClient(clientId);
            }
            break;
        }

        int received = static_cast<int>(recv(clientSocket, buffer.data(), buffer.size(), 0));

        if (received > 0) {
            TcpServerReceiveEventArgs args;
            args.clientId = clientId;
            args.data.assign(buffer.begin(), buffer.begin() + received);
            onReceive.notify(args);
        } else if (received == 0) {
            // Client closed connection
            TcpClientDisconnectEventArgs args;
            args.clientId = clientId;
            args.reason = "Connection closed by client";
            args.wasClean = true;
            onClientDisconnect.notify(args);

            logNotice() << "Client " << clientId << " disconnected";
            removeClient(clientId);
            break;
        } else {
            // Error
            int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK) continue;
#else
            if (err == EWOULDBLOCK || err == EAGAIN) continue;
#endif
            if (running_) {
                TcpClientDisconnectEventArgs args;
                args.clientId = clientId;
                args.reason = "Connection error";
                args.wasClean = false;
                onClientDisconnect.notify(args);

                removeClient(clientId);
            }
            break;
        }
    }
}

// =============================================================================
// Client management
// =============================================================================
void TcpServer::disconnectClient(int clientId) {
    std::shared_ptr<internal::TcpSendChannel> ch;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            ch = it->second.channel_;
            clients_.erase(it);
        }
    }

    // Outside clientsMutex_: closing joins the writer thread, which can be
    // mid-send, and holding the map lock there would stall the whole server.
    closeChannel(clientId, ch);

    // Wait for this client's receive thread before returning. Detaching it
    // instead — which is what this did — let stop() return, and therefore
    // ~TcpServer() finish, while those threads were still reading members of
    // the object being destroyed.
    //
    // The one thread that cannot be joined here is the caller's own: an
    // onReceive listener disconnecting its own client runs ON that thread, and
    // joining it would deadlock. It is detached, and it is already unwinding.
    std::thread finishing;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto threadIt = clientThreads_.find(clientId);
        if (threadIt != clientThreads_.end()) {
            if (threadIt->second.get_id() == std::this_thread::get_id()) {
                threadIt->second.detach();
            } else {
                finishing = std::move(threadIt->second);
            }
            clientThreads_.erase(threadIt);
        }
    }

    // Outside clientsMutex_: the thread being joined takes that same lock on its
    // way out (removeClient), so holding it here would deadlock the pair.
    if (finishing.joinable()) finishing.join();
}

void TcpServer::disconnectAllClients() {
    std::vector<int> ids;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        for (const auto& pair : clients_) {
            ids.push_back(pair.first);
        }
    }

    for (int id : ids) {
        disconnectClient(id);
    }

    // Collect threads to join (without holding lock during join to avoid deadlock)
    std::vector<std::thread> threadsToJoin;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        for (auto& pair : clientThreads_) {
            if (pair.second.joinable()) {
                threadsToJoin.push_back(std::move(pair.second));
            }
        }
        clientThreads_.clear();
        // Writers whose client was already gone from the registry, so nothing
        // above claimed them
        for (auto& pair : clientWriters_) {
            if (pair.second.joinable()) {
                threadsToJoin.push_back(std::move(pair.second));
            }
        }
        clientWriters_.clear();
    }

    // Join threads without holding mutex
    for (auto& t : threadsToJoin) {
        t.join();
    }
}

void TcpServer::removeClient(int clientId) {
    std::shared_ptr<internal::TcpSendChannel> ch;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return;
        ch = it->second.channel_;
        clients_.erase(it);
    }
    closeChannel(clientId, ch);
}

int TcpServer::getClientCount() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    return static_cast<int>(clients_.size());
}

std::vector<int> TcpServer::getClientIds() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    std::vector<int> ids;
    for (const auto& pair : clients_) {
        ids.push_back(pair.first);
    }
    return ids;
}

const TcpServerClient* TcpServer::getClient(int clientId) const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        return &it->second;
    }
    return nullptr;
}

// =============================================================================
// Send channel plumbing
// =============================================================================
static bool isWouldBlock(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
#else
    return err == EWOULDBLOCK || err == EAGAIN;
#endif
}

std::shared_ptr<internal::TcpSendChannel> TcpServer::findChannel(int clientId) const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return nullptr;
    return it->second.channel_;
}

std::vector<std::pair<int, std::shared_ptr<internal::TcpSendChannel>>>
TcpServer::snapshotChannels() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    std::vector<std::pair<int, std::shared_ptr<internal::TcpSendChannel>>> out;
    out.reserve(clients_.size());
    for (const auto& pair : clients_) out.emplace_back(pair.first, pair.second.channel_);
    return out;
}

void TcpServer::closeChannel(int clientId, const std::shared_ptr<internal::TcpSendChannel>& ch) {
    if (!ch) return;
    // Order matters: refuse new sends, wake everything parked on the channel,
    // then shut the socket down so a send already blocked inside the kernel
    // returns. The descriptor itself is closed by the writer thread on its way
    // out — joining it below is what makes that ordering safe.
    if (!ch->open.exchange(false)) return;

    // The shutdown goes under the mutex because the writer closes the
    // descriptor under it too: without that, a writer that woke, found the
    // queue empty and exited could have closed — and the kernel recycled —
    // this descriptor between the read and the call.
    //
    // The same lock is what makes the notify land: a waiter that has checked
    // its predicate but not yet parked holds the mutex, so this blocks until it
    // is genuinely waiting.
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        if (ch->socket != INVALID_SOCKET) {
#ifdef _WIN32
            ::shutdown(ch->socket, SD_BOTH);
#else
            ::shutdown(ch->socket, SHUT_RDWR);
#endif
        }
    }
    ch->queued.notify_all();
    ch->room.notify_all();

    // Wait for the writer to drain what is left of the queue — every queued id
    // still has to complete exactly once — and to close the descriptor.
    //
    // The one thread that cannot be joined here is the caller's own: an
    // onSendComplete or onError listener disconnecting its own client runs ON
    // that writer thread. It is detached, and it is already unwinding; it will
    // close the descriptor itself before it exits.
    std::thread writer;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto it = clientWriters_.find(clientId);
        if (it != clientWriters_.end()) {
            if (it->second.get_id() == std::this_thread::get_id()) {
                it->second.detach();
            } else {
                writer = std::move(it->second);
            }
            clientWriters_.erase(it);
        }
    }
    if (writer.joinable()) writer.join();
}

// How one payload gets written. Called only by that client's writer thread, and
// never under the channel mutex: a peer that stops draining must not stall the
// next sendAsync().
namespace {

struct WriteOutcome {
    const char* failure = nullptr;   // null = the whole payload was written
    int code = 0;
    size_t written = 0;
    bool truncated = false;          // partial payload written: the stream is unusable
};

} // namespace

static WriteOutcome writePayload(internal::TcpSendChannel& ch, const void* data, size_t size,
                                 float timeout) {
    WriteOutcome out;
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    // The timeout measures SILENCE, not the length of the send: a peer that
    // keeps draining is healthy however long the payload takes, and a peer that
    // has stopped reading is what the timeout is for. Measuring total elapsed
    // time instead would drop a healthy client for the offence of being on a
    // slow link with a big payload.
    auto lastProgress = std::chrono::steady_clock::now();

    while (remaining > 0) {
        // Re-read every iteration: closeChannel() clears this to cut a parked
        // send short, and that is the only thing that can.
        if (!ch.open) {
            out.failure = "Client disconnected";
            break;
        }

        int sent = static_cast<int>(::send(ch.socket, ptr, remaining, TC_SEND_FLAGS));
        if (sent > 0) {
            ptr += sent;
            remaining -= static_cast<size_t>(sent);
            out.written += static_cast<size_t>(sent);
            lastProgress = std::chrono::steady_clock::now();
            continue;
        }
        if (sent == 0) {
            out.failure = "Send failed";
            break;
        }

        int err = SOCKET_ERROR_CODE;
#ifndef _WIN32
        if (err == EINTR) continue;
#endif
        if (!isWouldBlock(err)) {
            out.failure = "Send failed";
            out.code = err;
            break;
        }

        // The peer is not draining its window. Park for one slice so a
        // disconnect can interrupt this, then re-check the deadline.
        if (waitReady(ch.socket, true, kWaitSliceMs) < 0) {
            out.failure = "Send failed";
            out.code = socketError(ch.socket);
            break;
        }
        if (timeout <= 0.0f) continue;

        const std::chrono::duration<float> idle = std::chrono::steady_clock::now() - lastProgress;
        if (idle.count() < timeout) continue;

        out.code = err;
        // A timeout that wrote nothing leaves the stream intact. One that fires
        // mid-payload leaves it truncated with the connection still open, so
        // the next send would splice a fresh message onto a partial one: the
        // client has to go.
        if (out.written > 0) {
            out.failure = "Send timed out mid-payload; client dropped";
            out.truncated = true;
        } else {
            out.failure = "Send timed out";
        }
        break;
    }

    return out;
}

// =============================================================================
// Send queue
// =============================================================================

// True when the caller is the writer thread for this channel — i.e. it is
// inside an onSendComplete or onError listener firing from that thread.
// Waiting for a queued item from there would wait on the very thread that has
// to write it.
static bool isOwnWriterThread(const std::shared_ptr<internal::TcpSendChannel>& ch) {
    std::lock_guard<std::mutex> lock(ch->mutex);
    return ch->writerId == std::this_thread::get_id();
}

SendResult TcpServer::enqueue(const std::shared_ptr<internal::TcpSendChannel>& ch, int clientId,
                              internal::TcpSendItem&& item) {
    (void)clientId;
    const size_t limit = sendAsyncBufferSize_.load();
    std::unique_lock<std::mutex> lock(ch->mutex);

    if (!ch->open || ch->dropped) return SendResult{SendError::Disconnected, 0};

    if (limit > 0 && ch->pendingBytes >= limit) {
        // A high-water mark, not a cap. The queue is only refused once it
        // ALREADY holds the limit, so a single message of any size still goes
        // through on an empty queue: there is no separate "too large" case, and
        // the queue overshoots by at most one message.
        if (!item.waiter) return SendResult{SendError::QueueFull, 0};

        // A synchronous send waits for room rather than failing, which is why
        // QueueFull is something only sendAsync() can return. The mark is
        // re-read here rather than captured: raising it while a send waits
        // should let that send through at the next completion, not leave it
        // parked against the old value.
        ch->room.wait(lock, [&] {
            const size_t mark = sendAsyncBufferSize_.load();
            return !ch->open || ch->dropped || mark == 0 || ch->pendingBytes < mark;
        });
        if (!ch->open || ch->dropped) return SendResult{SendError::Disconnected, 0};
    }

    const uint64_t id = ++nextSendId_;
    item.id = id;
    ch->pendingBytes += item.size;
    ch->queue.push_back(std::move(item));
    lock.unlock();

    ch->queued.notify_one();
    return SendResult{SendError::None, id};
}

void TcpServer::completeSend(int clientId, const std::shared_ptr<internal::TcpSendChannel>& ch,
                             const internal::TcpSendItem& item, SendError error, size_t bytesSent) {
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        ch->pendingBytes -= item.size;
    }
    ch->room.notify_all();

    // Wake a synchronous sender before running listeners: it has nothing to
    // learn from them, and no reason to stay parked through one.
    if (item.waiter) {
        {
            std::lock_guard<std::mutex> lock(item.waiter->mutex);
            item.waiter->error = error;
            item.waiter->bytesSent = bytesSent;
            item.waiter->done = true;
        }
        item.waiter->cv.notify_all();
    }

    TcpSendCompleteEventArgs args;
    args.clientId = clientId;
    args.sendId = item.id;
    args.error = error;
    args.bytesSent = bytesSent;
    onSendComplete.notify(args);
}

// =============================================================================
// Client writer thread
//
// One per client, draining that client's queue. Every id it takes off the queue
// completes exactly once, whether it was written, refused or caught by a
// teardown — so a caller counting completions never has to wonder which.
// =============================================================================
void TcpServer::writerThreadFunc(int clientId, std::shared_ptr<internal::TcpSendChannel> ch) {
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        ch->writerId = std::this_thread::get_id();
    }

    for (;;) {
        internal::TcpSendItem item;
        {
            std::unique_lock<std::mutex> lock(ch->mutex);
            ch->queued.wait(lock, [&] { return !ch->queue.empty() || !ch->open; });
            if (ch->queue.empty()) break;   // closed and drained
            item = std::move(ch->queue.front());
            ch->queue.pop_front();
        }

        // Closed, or already given up on: report the id and move on rather than
        // writing to a socket that is going away.
        if (!ch->open || ch->dropped) {
            completeSend(clientId, ch, item, SendError::Disconnected, 0);
            continue;
        }

        const WriteOutcome out = writePayload(*ch, item.data(), item.size, sendTimeout_.load());
        if (!out.failure) {
            completeSend(clientId, ch, item, SendError::None, out.written);
            continue;
        }

        if (out.truncated) {
            // The stream is unusable, so the client has to go — but this thread
            // cannot tear down the client it is running for (closeChannel()
            // would join it). Mark the channel and shut the socket down; the
            // receive thread wakes and does the removal.
            ch->dropped = true;
            std::lock_guard<std::mutex> lock(ch->mutex);
            if (ch->socket != INVALID_SOCKET) {
#ifdef _WIN32
                ::shutdown(ch->socket, SD_BOTH);
#else
                ::shutdown(ch->socket, SHUT_RDWR);
#endif
            }
        }
        notifyError(out.failure, out.code, clientId);
        completeSend(clientId, ch, item, SendError::Disconnected, out.written);
    }

    // This thread is the only one that writes to the descriptor, so it is the
    // one that closes it. Teardown joins it first and therefore can never pull
    // the descriptor out from under a send in flight — nor hand it to a fresh
    // connection while this loop still holds it.
    std::lock_guard<std::mutex> lock(ch->mutex);
    if (ch->socket != INVALID_SOCKET) {
        CLOSE_SOCKET(ch->socket);
        ch->socket = INVALID_SOCKET;
    }
}

// =============================================================================
// Data send
// =============================================================================

// Blocking: returns once the whole payload has been handed to the kernel, or on
// error. It queues the payload like sendAsync() does and waits for that one id,
// so sync and async sends to the same client stay in order and share one code
// path and one idle timeout. The payload is lent, not copied: the caller is
// parked here until the item is done with it.
bool TcpServer::send(int clientId, const void* data, size_t size) {
    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return false;
    }

    if (isOwnWriterThread(ch)) {
        // A listener firing on this client's writer thread cannot wait for that
        // same thread to write anything. sendAsync() from there is fine.
        notifyError("send() cannot wait inside a listener on this client's send thread; "
                    "use sendAsync()", 0, clientId);
        return false;
    }

    internal::TcpSendItem item;
    item.borrowed = data;
    item.size = size;
    item.waiter = std::make_shared<internal::TcpSendWaiter>();
    std::shared_ptr<internal::TcpSendWaiter> waiter = item.waiter;

    const SendResult queuedResult = enqueue(ch, clientId, std::move(item));
    if (!queuedResult) {
        // Write failures are reported by the writer thread; this is the one
        // case the caller has to report itself.
        notifyError("Client disconnected", 0, clientId);
        return false;
    }

    std::unique_lock<std::mutex> lock(waiter->mutex);
    waiter->cv.wait(lock, [&] { return waiter->done; });
    return waiter->error == SendError::None;
}

bool TcpServer::send(int clientId, const std::vector<char>& data) {
    return send(clientId, data.data(), data.size());
}

bool TcpServer::send(int clientId, const std::string& message) {
    return send(clientId, message.data(), message.size());
}

SendResult TcpServer::sendAsync(int clientId, const void* data, size_t size) {
    if (!running_) return SendResult{SendError::NotRunning, 0};

    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return SendResult{SendError::ClientNotFound, 0};
    }

    internal::TcpSendItem item;
    item.owned = std::make_shared<const std::vector<char>>(
        static_cast<const char*>(data), static_cast<const char*>(data) + size);
    item.size = size;

    const SendResult result = enqueue(ch, clientId, std::move(item));
    if (!result) notifyError(result.error == SendError::QueueFull ? "Send queue full"
                                                                 : "Client disconnected",
                             0, clientId);
    return result;
}

SendResult TcpServer::sendAsync(int clientId, std::vector<char>&& data) {
    if (!running_) return SendResult{SendError::NotRunning, 0};

    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return SendResult{SendError::ClientNotFound, 0};
    }

    internal::TcpSendItem item;
    item.size = data.size();
    item.owned = std::make_shared<const std::vector<char>>(std::move(data));

    const SendResult result = enqueue(ch, clientId, std::move(item));
    if (!result) notifyError(result.error == SendError::QueueFull ? "Send queue full"
                                                                 : "Client disconnected",
                             0, clientId);
    return result;
}

SendResult TcpServer::sendAsync(int clientId, const std::string& message) {
    return sendAsync(clientId, message.data(), message.size());
}

void TcpServer::broadcast(const void* data, size_t size) {
    // Queue to every client first, then wait for all of them. Sending to each
    // in turn would put a slow peer in front of the fast ones behind it; the
    // call still returns only once everyone has the payload (or has failed).
    struct Pending {
        int clientId;
        std::shared_ptr<internal::TcpSendWaiter> waiter;
    };
    std::vector<Pending> pending;

    // One pass over the registry, not one lookup per client: a broadcast from
    // a draw loop used to take the client lock once for the id list and again
    // for every id in it. A client that goes away mid-broadcast is reported by
    // its own channel (closed) rather than by a failed lookup.
    for (auto& [id, ch] : snapshotChannels()) {
        if (isOwnWriterThread(ch)) continue;   // see send()

        internal::TcpSendItem item;
        item.borrowed = data;   // every waiter is joined below, so `data` outlives them
        item.size = size;
        item.waiter = std::make_shared<internal::TcpSendWaiter>();
        std::shared_ptr<internal::TcpSendWaiter> waiter = item.waiter;

        if (!enqueue(ch, id, std::move(item))) {
            notifyError("Client disconnected", 0, id);
            continue;
        }
        pending.push_back({id, std::move(waiter)});
    }

    for (auto& p : pending) {
        std::unique_lock<std::mutex> lock(p.waiter->mutex);
        p.waiter->cv.wait(lock, [&] { return p.waiter->done; });
    }
}

void TcpServer::broadcast(const std::vector<char>& data) {
    broadcast(data.data(), data.size());
}

void TcpServer::broadcast(const std::string& message) {
    broadcast(message.data(), message.size());
}

int TcpServer::broadcastAsync(const void* data, size_t size) {
    if (!running_) return 0;

    // Buffered once and shared, not copied per client: a broadcast of a frame
    // to twenty peers should not mean twenty copies of the frame.
    auto payload = std::make_shared<const std::vector<char>>(
        static_cast<const char*>(data), static_cast<const char*>(data) + size);

    int queued = 0;
    for (auto& [id, ch] : snapshotChannels()) {
        internal::TcpSendItem item;
        item.owned = payload;
        item.size = size;
        if (enqueue(ch, id, std::move(item))) ++queued;
    }
    return queued;
}

int TcpServer::broadcastAsync(const std::vector<char>& data) {
    return broadcastAsync(data.data(), data.size());
}

int TcpServer::broadcastAsync(const std::string& message) {
    return broadcastAsync(message.data(), message.size());
}

size_t TcpServer::getSendAsyncPendingBytes(int clientId) const {
    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) return 0;
    std::lock_guard<std::mutex> lock(ch->mutex);
    return ch->pendingBytes;
}

// =============================================================================
// Settings
// =============================================================================
void TcpServer::setSendTimeout(float seconds) {
    sendTimeout_ = seconds < 0.0f ? 0.0f : seconds;
}

void TcpServer::setReceiveBufferSize(size_t size) {
    receiveBufferSize_ = size;
}

void TcpServer::setSendAsyncBufferSize(size_t bytes) {
    sendAsyncBufferSize_ = bytes;
}

size_t TcpServer::getSendAsyncBufferSize() const {
    return sendAsyncBufferSize_.load();
}

// =============================================================================
// Information retrieval
// =============================================================================
int TcpServer::getPort() const {
    return port_;
}

// =============================================================================
// Error notification
// =============================================================================
void TcpServer::notifyError(const std::string& msg, int code, int clientId) {
    TcpServerErrorEventArgs args;
    args.message = msg;
    args.errorCode = code;
    args.clientId = clientId;
    onError.notify(args);
}

} // namespace trussc
