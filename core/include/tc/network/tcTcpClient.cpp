// =============================================================================
// tcTcpClient.cpp - TCP client socket implementation
// =============================================================================

#include "tc/network/tcTcpClient.h"
#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"
#include "tc/events/tcCoreEvents.h"
#include <cstring>

namespace trussc {

// =============================================================================
// Constructor / Destructor
// =============================================================================
TcpClient::TcpClient() {
    internal::ensureWinsock();
#ifdef __EMSCRIPTEN__
    useThread_ = false;
#endif
}

TcpClient::~TcpClient() {
    disconnect();
}

TcpClient::TcpClient(TcpClient&& other) noexcept
    : socket_(other.socket_)
    , remoteHost_(std::move(other.remoteHost_))
    , remotePort_(other.remotePort_)
    , running_(other.running_.load())
    , connected_(other.connected_.load())
    , receiveBufferSize_(other.receiveBufferSize_)
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
        receiveBufferSize_ = other.receiveBufferSize_;
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
    if (connected_ || running_ || connectPending_) {
        disconnect();
    }

    // After the peer closed the connection (or it failed) the flags above are
    // all clear, but the socket and the finished receive thread are still
    // here. Release them before starting over: overwriting socket_ leaks the
    // descriptor, and assigning a new thread to a still-joinable
    // receiveThread_ calls std::terminate.
    resetConnection();

    // Create socket
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (socket_ == INVALID_SOCKET) {
#else
    if (socket_ < 0) {
#endif
        notifyError("Failed to create socket", SOCKET_ERROR_CODE);
        return false;
    }

    // A send() racing the peer's close must fail, not raise SIGPIPE
    internal::setNoSigpipe(socket_);

    // Set non-blocking if not using threads to avoid blocking connect
    if (!useThread_) {
        setBlocking(false);
    }

    // Resolve hostname
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int ret = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0) {
        notifyError("Failed to resolve host: " + host, ret);
        CLOSE_SOCKET(socket_);
#ifdef _WIN32
        socket_ = INVALID_SOCKET;
#else
        socket_ = -1;
#endif
        return false;
    }

    // Connect
    ret = ::connect(socket_, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    remoteHost_ = host;
    remotePort_ = port;

    if (ret == SOCKET_ERROR) {
        int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
        if (err == WSAEWOULDBLOCK) {
#else
        if (err == EINPROGRESS) {
#endif
            // Async connection started
            connectPending_ = true;
            running_ = true;
        } else {
            notifyError("Failed to connect to " + host + ":" + std::to_string(port), err);
            CLOSE_SOCKET(socket_);
#ifdef _WIN32
            socket_ = INVALID_SOCKET;
#else
            socket_ = -1;
#endif
            return false;
        }
    } else {
        // Connected immediately
        connected_ = true;
        running_ = true;
        
        logNotice() << "TCP connected to " << host << ":" << port;

        TcpConnectEventArgs args;
        args.success = true;
        args.message = "Connected";
        onConnect.notify(args);
    }

    if (running_) {
        if (useThread_) {
            // Start receive thread (ensure blocking mode for thread unless explicitly set otherwise)
            setBlocking(true);
            receiveThread_ = std::thread(&TcpClient::receiveThreadFunc, this,
                                         ++receiveGeneration_);
        } else {
            // Register update listener
            updateListener_ = events().update.listen(this, &TcpClient::processNetwork);
        }
    }

    return true;
}

void TcpClient::connectAsync(const std::string& host, int port) {
    if (useThread_) {
        // Wait for existing connection thread if any
        if (connectThread_.joinable()) {
            connectThread_.join();
        }
        connectThread_ = std::thread(&TcpClient::connectThreadFunc, this, host, port);
    } else {
        // Non-blocking connect handled in connect() + processNetwork()
        connect(host, port);
    }
}

void TcpClient::connectThreadFunc(const std::string& host, int port) {
    bool success = connect(host, port);
    if (!success) {
        TcpConnectEventArgs args;
        args.success = false;
        args.message = "Connection failed";
        onConnect.notify(args);
    }
}

void TcpClient::disconnect() {
    running_ = false;
    connectPending_ = false;
    updateListener_.disconnect();

    resetConnection();

    if (connectThread_.joinable()) {
        if (connectThread_.get_id() == std::this_thread::get_id()) {
            connectThread_.detach();
        } else {
            connectThread_.join();
        }
    }

    // The receive thread reports only a close it ran into itself (running_
    // still set). The EOF that the shutdown() above wakes it with is this
    // call's own, and is reported here, once, after the join.
    if (connected_.exchange(false)) {
        TcpDisconnectEventArgs args;
        args.reason = "Disconnected by client";
        args.wasClean = true;
        onDisconnect.notify(args);
    }
}

// Close the socket and release the receive thread. connectThread_ is left
// alone: connect() runs on it for connectAsync(), and calls this.
void TcpClient::resetConnection() {
#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        CLOSE_SOCKET(socket_);
        socket_ = INVALID_SOCKET;
    }
#else
    if (socket_ >= 0) {
        shutdown(socket_, SHUT_RDWR);
        CLOSE_SOCKET(socket_);
        socket_ = -1;
    }
#endif

    if (receiveThread_.joinable()) {
        if (receiveThread_.get_id() == std::this_thread::get_id()) {
            // Called from within the receive thread (e.g. a listener that
            // disconnects or reconnects). Cannot join self. Detach: the loop in
            // receiveThreadFunc() ends on its own once running_ is cleared or
            // a newer receive thread has taken over.
            receiveThread_.detach();
        } else {
            receiveThread_.join();
        }
    }
}

bool TcpClient::isConnected() const {
    return connected_;
}

// =============================================================================
// Data transmission
// =============================================================================
bool TcpClient::send(const void* data, size_t size) {
    if (!connected_) {
        notifyError("Not connected");
        return false;
    }

    std::lock_guard<std::mutex> lock(sendMutex_);

    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    while (remaining > 0) {
        int sent = static_cast<int>(::send(socket_, ptr, remaining, TC_SEND_FLAGS));
        if (sent == SOCKET_ERROR) {
            int err = SOCKET_ERROR_CODE;
            if (err == WOULD_BLOCK_ERROR) {
                // In non-blocking mode, we should ideally buffer this, 
                // but for now we just return false or wait.
                // Simple implementation: wait a bit or fail.
                continue; 
            }
            notifyError("Send failed", err);
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

bool TcpClient::send(const std::vector<char>& data) {
    return send(data.data(), data.size());
}

bool TcpClient::send(const std::string& message) {
    return send(message.data(), message.size());
}

// =============================================================================
// Update / Receive logic
// =============================================================================
void TcpClient::processNetwork() {
    if (!running_) return;

    // Handle pending connection
    if (connectPending_) {
#ifdef _WIN32
        struct fd_set writefds, exceptfds;
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(socket_, &writefds);
        FD_SET(socket_, &exceptfds);
        struct timeval tv = {0, 0};
        int res = select(0, NULL, &writefds, &exceptfds, &tv);
        if (res > 0) {
            if (FD_ISSET(socket_, &exceptfds)) {
                int err = 0;
                int len = sizeof(err);
                getsockopt(socket_, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
                notifyError("Connection failed", err);
                disconnect();
                TcpConnectEventArgs args;
                args.success = false;
                args.message = "Connection failed";
                onConnect.notify(args);
                return;
            }
            if (FD_ISSET(socket_, &writefds)) {
                connectPending_ = false;
                connected_ = true;
                logNotice() << "TCP connected (async) to " << remoteHost_ << ":" << remotePort_;
                TcpConnectEventArgs args;
                args.success = true;
                args.message = "Connected";
                onConnect.notify(args);
            }
        }
#else
        struct pollfd pfd;
        pfd.fd = socket_;
        pfd.events = POLLOUT;
        int res = poll(&pfd, 1, 0);
        if (res > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(socket_, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                connectPending_ = false;
                connected_ = true;
                logNotice() << "TCP connected (async) to " << remoteHost_ << ":" << remotePort_;
                TcpConnectEventArgs args;
                args.success = true;
                args.message = "Connected";
                onConnect.notify(args);
            } else {
                notifyError("Connection failed", err);
                disconnect();
                TcpConnectEventArgs args;
                args.success = false;
                args.message = "Connection failed";
                onConnect.notify(args);
                return;
            }
        }
#endif
    }

    if (!connected_) return;

    // Receive data. The buffer is this client's own: every client's receive
    // thread runs this at the same time.
    if (recvBuf_.size() != receiveBufferSize_) {
        recvBuf_.resize(receiveBufferSize_);
    }

    while (connected_) {
        int received = static_cast<int>(recv(socket_, recvBuf_.data(), recvBuf_.size(), 0));

        if (received > 0) {
            TcpReceiveEventArgs args;
            args.data.assign(recvBuf_.begin(), recvBuf_.begin() + received);
            onReceive.notify(args);
            
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
                TcpDisconnectEventArgs args;
                args.reason = "Connection closed by remote";
                args.wasClean = true;
                onDisconnect.notify(args);
            }
            break;
        } else {
            // Error
            int err = SOCKET_ERROR_CODE;
            if (err == WOULD_BLOCK_ERROR) break;
            
            // As above: an error caused by a local disconnect() is its to report
            if (running_.exchange(false)) {
                connected_ = false;
                TcpDisconnectEventArgs args;
                args.reason = "Connection error";
                args.wasClean = false;
                onDisconnect.notify(args);
            }
            break;
        }
    }
}

void TcpClient::receiveThreadFunc(unsigned generation) {
    // running_ alone cannot end this loop when a listener on this thread
    // reconnects: connect() detaches this thread, starts the new connection's
    // own, and running_ is true again for that one. The generation says which
    // thread is current.
    while (running_ && receiveGeneration_ == generation) {
        processNetwork();
        if (running_ && receiveGeneration_ == generation) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        u_long mode = blocking ? 0 : 1;
        ioctlsocket(socket_, FIONBIO, &mode);
    }
#else
    if (socket_ >= 0) {
        int flags = fcntl(socket_, F_GETFL, 0);
        if (blocking) {
            fcntl(socket_, F_SETFL, flags & ~O_NONBLOCK);
        } else {
            fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
        }
    }
#endif
}

// =============================================================================
// Information retrieval
// =============================================================================
std::string TcpClient::getRemoteHost() const {
    return remoteHost_;
}

int TcpClient::getRemotePort() const {
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
