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
            std::lock_guard<std::mutex> lock(clientsMutex_);
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

        logNotice() << "Client " << clientId << " connected from " << hostStr << ":" << clientPort;

        // Notify connection event
        TcpClientConnectEventArgs args;
        args.clientId = clientId;
        args.host = hostStr;
        args.port = clientPort;
        onClientConnect.notify(args);

        // Start receive thread for client
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
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
        std::lock_guard<std::mutex> lock(clientsMutex_);
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
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            ch = it->second.channel_;
            clients_.erase(it);
        }
    }

    // Outside clientsMutex_: closing can block behind an in-flight send, and
    // holding the map lock there would stall the whole server.
    closeChannel(ch);

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
        std::lock_guard<std::mutex> lock(clientsMutex_);
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
        std::lock_guard<std::mutex> lock(clientsMutex_);
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
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& pair : clientThreads_) {
            if (pair.second.joinable()) {
                threadsToJoin.push_back(std::move(pair.second));
            }
        }
        clientThreads_.clear();
    }

    // Join threads without holding mutex
    for (auto& t : threadsToJoin) {
        t.join();
    }
}

void TcpServer::removeClient(int clientId) {
    std::shared_ptr<internal::TcpSendChannel> ch;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return;
        ch = it->second.channel_;
        clients_.erase(it);
    }
    closeChannel(ch);
}

int TcpServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return static_cast<int>(clients_.size());
}

std::vector<int> TcpServer::getClientIds() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    std::vector<int> ids;
    for (const auto& pair : clients_) {
        ids.push_back(pair.first);
    }
    return ids;
}

const TcpServerClient* TcpServer::getClient(int clientId) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        return &it->second;
    }
    return nullptr;
}

// =============================================================================
// Data send
// =============================================================================
static bool isWouldBlock(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
#else
    return err == EWOULDBLOCK || err == EAGAIN;
#endif
}

std::shared_ptr<internal::TcpSendChannel> TcpServer::findChannel(int clientId) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return nullptr;
    return it->second.channel_;
}

void TcpServer::closeChannel(const std::shared_ptr<internal::TcpSendChannel>& ch) {
    if (!ch) return;
    // Order matters: refuse new sends, then shut the socket down so a send
    // already blocked inside the kernel returns instead of pinning the mutex,
    // and only then take the mutex and close the descriptor for good.
    if (!ch->open.exchange(false)) return;
#ifdef _WIN32
    ::shutdown(ch->socket, SD_BOTH);
#else
    ::shutdown(ch->socket, SHUT_RDWR);
#endif
    std::lock_guard<std::mutex> lock(ch->mutex);
    CLOSE_SOCKET(ch->socket);
    ch->socket = INVALID_SOCKET;
}

// Blocking: returns once the whole payload has been handed to the kernel, or on
// error. Only this client's send mutex is held, never clientsMutex_, so a peer
// that stops reading cannot stall the rest of the server.
bool TcpServer::send(int clientId, const void* data, size_t size) {
    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return false;
    }

    // The failure is recorded here and reported after the lock is released.
    // onError listeners run inline by default, and the obvious thing to write in
    // one is disconnectClient() — which re-enters this same non-recursive mutex
    // through closeChannel() and deadlocks the caller.
    const char* failure = nullptr;
    int failureCode = 0;
    bool truncated = false;   // partial payload written: the stream is unusable

    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        if (!ch->open) {
            failure = "Client disconnected";
        } else {
            const char* ptr = static_cast<const char*>(data);
            size_t remaining = size;
            // The timeout measures SILENCE, not the length of the send: a peer
            // that keeps draining is healthy however long the payload takes,
            // and a peer that has stopped reading is what the timeout is for.
            // Measuring total elapsed time instead would drop a healthy client
            // for the offence of being on a slow link with a big payload.
            const float timeout = sendTimeout_.load();   // 0 = wait indefinitely
            auto lastProgress = std::chrono::steady_clock::now();

            while (remaining > 0) {
                // Re-read every iteration: closeChannel() clears this to cut a
                // parked send short, and that is the only thing that can.
                if (!ch->open) {
                    failure = "Client disconnected";
                    break;
                }

                int sent = static_cast<int>(::send(ch->socket, ptr, remaining, TC_SEND_FLAGS));
                if (sent > 0) {
                    ptr += sent;
                    remaining -= sent;
                    lastProgress = std::chrono::steady_clock::now();
                    continue;
                }
                if (sent == 0) {
                    failure = "Send failed";
                    break;
                }

                int err = SOCKET_ERROR_CODE;
#ifndef _WIN32
                if (err == EINTR) continue;
#endif
                if (!isWouldBlock(err)) {
                    failure = "Send failed";
                    failureCode = err;
                    break;
                }

                // The peer is not draining its window. Park for one slice so a
                // disconnect can interrupt this, then re-check the deadline.
                if (waitReady(ch->socket, true, kWaitSliceMs) < 0) {
                    failure = "Send failed";
                    failureCode = socketError(ch->socket);
                    break;
                }
                if (timeout <= 0.0f) continue;

                const std::chrono::duration<float> idle =
                    std::chrono::steady_clock::now() - lastProgress;
                if (idle.count() < timeout) continue;

                failureCode = err;
                // A timeout that wrote nothing leaves the stream intact. One
                // that fires mid-payload leaves it truncated with the
                // connection still open, so the next send() would splice a
                // fresh message onto a partial one: drop the client instead.
                if (remaining != size) {
                    failure = "Send timed out mid-payload; client dropped";
                    truncated = true;
                } else {
                    failure = "Send timed out";
                }
                break;
            }
        }
    }

    if (!failure) return true;

    // Outside the send lock: safe for a listener to disconnect the client.
    // A timeout that wrote nothing leaves the stream intact — only a truncated
    // one forces the drop.
    if (truncated) removeClient(clientId);
    notifyError(failure, failureCode, clientId);
    return false;
}

bool TcpServer::send(int clientId, const std::vector<char>& data) {
    return send(clientId, data.data(), data.size());
}

bool TcpServer::send(int clientId, const std::string& message) {
    return send(clientId, message.data(), message.size());
}

void TcpServer::broadcast(const void* data, size_t size) {
    std::vector<int> ids = getClientIds();
    for (int id : ids) {
        send(id, data, size);
    }
}

void TcpServer::broadcast(const std::vector<char>& data) {
    broadcast(data.data(), data.size());
}

void TcpServer::broadcast(const std::string& message) {
    broadcast(message.data(), message.size());
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
