// =============================================================================
// tcTcpClient.cpp - TCP client socket implementation
// =============================================================================

#include "tc/network/tcTcpClient.h"
#include "tc/utils/tcLog.h"
#include <cstring>

#ifdef _WIN32
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
    #define WOULD_BLOCK_ERROR WSAEWOULDBLOCK
#else
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
    #define WOULD_BLOCK_ERROR EWOULDBLOCK
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace trussc {

std::atomic<int> TcpClient::instanceCount_{0};

// =============================================================================
// Winsock initialization (Windows only)
// =============================================================================
void TcpClient::initWinsock() {
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

void TcpClient::cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// =============================================================================
// Constructor / Destructor
// =============================================================================
TcpClient::TcpClient() {
    if (instanceCount_++ == 0) {
        initWinsock();
    }
#ifdef __EMSCRIPTEN__
    useThread_ = false;
#endif
}

TcpClient::~TcpClient() {
    disconnect();
    if (--instanceCount_ == 0) {
        cleanupWinsock();
    }
}

TcpClient::TcpClient(TcpClient&& other) noexcept
    : socket_(other.socket_)
    , remoteHost_(std::move(other.remoteHost_))
    , remotePort_(other.remotePort_)
    , running_(other.running_.load())
    , connected_(other.connected_.load())
    , receiveBufferSize_(other.receiveBufferSize_)
{
#ifdef _WIN32
    other.socket_ = INVALID_SOCKET;
#else
    other.socket_ = -1;
#endif
    other.running_ = false;
    other.connected_ = false;
    instanceCount_++;
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
    if (connected_ || connectPending_) {
        disconnect();
    }

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
            receiveThread_ = std::thread(&TcpClient::receiveThreadFunc, this);
        } else {
            // Register update listener
            events().update.listen(updateListener_, this, &TcpClient::processNetwork);
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
        receiveThread_.join();
    }

    if (connectThread_.joinable()) {
        connectThread_.join();
    }

    if (connected_) {
        connected_ = false;
        TcpDisconnectEventArgs args;
        args.reason = "Disconnected by client";
        args.wasClean = true;
        onDisconnect.notify(args);
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
        int sent = static_cast<int>(::send(socket_, ptr, remaining, 0));
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

    // Receive data
    static std::vector<char> buffer;
    if (buffer.size() != receiveBufferSize_) {
        buffer.resize(receiveBufferSize_);
    }

    while (connected_) {
        int received = static_cast<int>(recv(socket_, buffer.data(), buffer.size(), 0));

        if (received > 0) {
            TcpReceiveEventArgs args;
            args.data.assign(buffer.begin(), buffer.begin() + received);
            onReceive.notify(args);
            
            // If using threads, we might block again. 
            // If not, we should return to let the app run.
            if (!useThread_) break; 
        } else if (received == 0) {
            // Connection closed
            running_ = false;
            connected_ = false;
            TcpDisconnectEventArgs args;
            args.reason = "Connection closed by remote";
            args.wasClean = true;
            onDisconnect.notify(args);
            break;
        } else {
            // Error
            int err = SOCKET_ERROR_CODE;
            if (err == WOULD_BLOCK_ERROR) break;
            
            if (running_) {
                running_ = false;
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

void TcpClient::receiveThreadFunc() {
    while (running_) {
        processNetwork();
        if (running_) {
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
