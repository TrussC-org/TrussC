// =============================================================================
// tcUdpSocket.cpp - UDP socket implementation
// =============================================================================

#include "tc/network/tcUdpSocket.h"

#include <cstring>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
#endif

namespace trussc {

// Winsock initialization flag
bool UdpSocket::winsockInitialized_ = false;

// ---------------------------------------------------------------------------
// Winsock initialization (Windows)
// ---------------------------------------------------------------------------
bool UdpSocket::initWinsock() {
#ifdef _WIN32
    if (!winsockInitialized_) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            logError() << "WSAStartup failed: " << result;
            return false;
        }
        winsockInitialized_ = true;
    }
#endif
    return true;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
UdpSocket::UdpSocket() {
    initWinsock();
}

UdpSocket::~UdpSocket() {
    close();
}

// ---------------------------------------------------------------------------
// Move operations
// ---------------------------------------------------------------------------
UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : socket_(other.socket_)
    , localPort_(other.localPort_)
    , connectedHost_(std::move(other.connectedHost_))
    , connectedPort_(other.connectedPort_)
    , receiving_(other.receiving_.load())
    , shouldStop_(other.shouldStop_.load())
{
    other.socket_ = INVALID_SOCKET_HANDLE;
    other.localPort_ = 0;
    other.connectedPort_ = 0;
    other.receiving_ = false;
    other.shouldStop_ = true;

    if (other.receiveThread_.joinable()) {
        other.receiveThread_.join();
    }
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();

        socket_ = other.socket_;
        localPort_ = other.localPort_;
        connectedHost_ = std::move(other.connectedHost_);
        connectedPort_ = other.connectedPort_;
        receiving_ = other.receiving_.load();
        shouldStop_ = other.shouldStop_.load();

        other.socket_ = INVALID_SOCKET_HANDLE;
        other.localPort_ = 0;
        other.connectedPort_ = 0;
        other.receiving_ = false;
        other.shouldStop_ = true;

        if (other.receiveThread_.joinable()) {
            other.receiveThread_.join();
        }
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Socket creation
// ---------------------------------------------------------------------------
bool UdpSocket::create() {
    if (socket_ != INVALID_SOCKET_HANDLE) {
        return true;  // Already created
    }

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET_HANDLE) {
        notifyError("Failed to create socket", SOCKET_ERROR_CODE);
        return false;
    }

    return true;
}

bool UdpSocket::ensureSocket() {
    if (socket_ == INVALID_SOCKET_HANDLE) {
        return create();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Bind
// ---------------------------------------------------------------------------
bool UdpSocket::bind(int port, bool startReceiving) {
    if (!ensureSocket()) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        notifyError("Failed to bind to port " + std::to_string(port), SOCKET_ERROR_CODE);
        return false;
    }

    localPort_ = port;
    logNotice() << "UDP socket bound to port " << port;

    if (startReceiving) {
        this->startReceiving();
    }

    return true;
}

// ---------------------------------------------------------------------------
// Set destination
// ---------------------------------------------------------------------------
bool UdpSocket::connect(const std::string& host, int port) {
    if (!ensureSocket()) {
        return false;
    }

    // Resolve hostname
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (status != 0 || !result) {
        notifyError("Failed to resolve host: " + host, status);
        return false;
    }

    // Set destination with connect() (for UDP, this just fixes the destination for send(), not an actual connection)
    if (::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) < 0) {
        notifyError("Failed to connect to " + host + ":" + std::to_string(port), SOCKET_ERROR_CODE);
        freeaddrinfo(result);
        return false;
    }

    freeaddrinfo(result);
    connectedHost_ = host;
    connectedPort_ = port;

    logNotice() << "UDP socket connected to " << host << ":" << port;
    return true;
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------
void UdpSocket::close() {
    shouldStop_ = true;

    // Shutdown and close socket first (causes recvfrom to return with error)
    if (socket_ != INVALID_SOCKET_HANDLE) {
#ifdef _WIN32
        shutdown(socket_, SD_BOTH);  // Windows: unblock blocking receive
#else
        shutdown(socket_, SHUT_RDWR);
#endif
        CLOSE_SOCKET(socket_);
        socket_ = INVALID_SOCKET_HANDLE;
    }

    // Wait for thread to finish
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
    receiving_ = false;

    localPort_ = 0;
    connectedHost_.clear();
    connectedPort_ = 0;
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------
bool UdpSocket::sendTo(const std::string& host, int port, const void* data, size_t size) {
    if (!ensureSocket()) {
        return false;
    }

    // Resolve hostname
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (status != 0 || !result) {
        notifyError("Failed to resolve host: " + host, status);
        return false;
    }

    auto sent = sendto(socket_,
                       static_cast<const char*>(data),
                       static_cast<int>(size),
                       0,
                       result->ai_addr,
                       static_cast<int>(result->ai_addrlen));

    freeaddrinfo(result);

    if (sent < 0) {
        notifyError("Failed to send data", SOCKET_ERROR_CODE);
        return false;
    }

    return true;
}

bool UdpSocket::sendTo(const std::string& host, int port, const std::string& message) {
    return sendTo(host, port, message.data(), message.size());
}

bool UdpSocket::send(const void* data, size_t size) {
    if (socket_ == INVALID_SOCKET_HANDLE) {
        notifyError("Socket not created");
        return false;
    }

    if (connectedHost_.empty()) {
        notifyError("No destination set. Call connect() first.");
        return false;
    }

    auto sent = ::send(socket_, static_cast<const char*>(data), static_cast<int>(size), 0);
    if (sent < 0) {
        notifyError("Failed to send data", SOCKET_ERROR_CODE);
        return false;
    }

    return true;
}

bool UdpSocket::send(const std::string& message) {
    return send(message.data(), message.size());
}

// ---------------------------------------------------------------------------
// Receive (synchronous)
// ---------------------------------------------------------------------------
int UdpSocket::receive(void* buffer, size_t bufferSize) {
    std::string host;
    int port;
    return receive(buffer, bufferSize, host, port);
}

int UdpSocket::receive(void* buffer, size_t bufferSize, std::string& remoteHost, int& remotePort) {
    if (socket_ == INVALID_SOCKET_HANDLE) {
        return -1;
    }

    sockaddr_in fromAddr{};
    socklen_t fromLen = sizeof(fromAddr);

    auto received = recvfrom(socket_,
                             static_cast<char*>(buffer),
                             static_cast<int>(bufferSize),
                             0,
                             reinterpret_cast<sockaddr*>(&fromAddr),
                             &fromLen);

    if (received > 0) {
        char hostStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fromAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
        remoteHost = hostStr;
        remotePort = ntohs(fromAddr.sin_port);
    }

    return static_cast<int>(received);
}

// ---------------------------------------------------------------------------
// Receive thread
// ---------------------------------------------------------------------------
void UdpSocket::startReceiving() {
    if (receiving_.load()) {
        return;  // Already receiving
    }

    shouldStop_ = false;
    receiving_ = true;

    receiveThread_ = std::thread(&UdpSocket::receiveThreadFunc, this);
}

void UdpSocket::stopReceiving() {
    shouldStop_ = true;

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }

    receiving_ = false;
}

void UdpSocket::receiveThreadFunc() {
    std::vector<char> buffer(RECEIVE_BUFFER_SIZE);

    while (!shouldStop_.load()) {
        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);

        auto received = recvfrom(socket_,
                                 buffer.data(),
                                 static_cast<int>(buffer.size()),
                                 0,
                                 reinterpret_cast<sockaddr*>(&fromAddr),
                                 &fromLen);

        if (shouldStop_.load()) {
            break;
        }

        if (received > 0) {
            UdpReceiveEventArgs args;
            args.data.assign(buffer.begin(), buffer.begin() + received);

            char hostStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
            args.remoteHost = hostStr;
            args.remotePort = ntohs(fromAddr.sin_port);

            onReceive.notify(args);
        } else if (received < 0) {
            int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK || err == WSAEINTR) continue;
#else
            if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) continue;
#endif
            if (!shouldStop_.load()) {
                notifyError("Receive error", err);
            }
            break;
        }
    }

    receiving_ = false;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
bool UdpSocket::setNonBlocking(bool nonBlocking) {
    if (socket_ == INVALID_SOCKET_HANDLE) return false;

#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    return ioctlsocket(socket_, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0) return false;
    if (nonBlocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(socket_, F_SETFL, flags) == 0;
#endif
}

bool UdpSocket::setBroadcast(bool enable) {
    if (!ensureSocket()) return false;
    int val = enable ? 1 : 0;
    return setsockopt(socket_, SOL_SOCKET, SO_BROADCAST,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool UdpSocket::setReuseAddress(bool enable) {
    if (!ensureSocket()) return false;
    int val = enable ? 1 : 0;
    return setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool UdpSocket::setReceiveBufferSize(int size) {
    if (socket_ == INVALID_SOCKET_HANDLE) return false;
    return setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size)) == 0;
}

bool UdpSocket::setSendBufferSize(int size) {
    if (socket_ == INVALID_SOCKET_HANDLE) return false;
    return setsockopt(socket_, SOL_SOCKET, SO_SNDBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size)) == 0;
}

bool UdpSocket::setReceiveTimeout(int timeoutMs) {
    if (socket_ == INVALID_SOCKET_HANDLE) return false;

#ifdef _WIN32
    DWORD tv = timeoutMs;
    return setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Error notification
// ---------------------------------------------------------------------------
void UdpSocket::notifyError(const std::string& message, int code) {
    logError() << "UdpSocket: " << message << " (code: " << code << ")";

    UdpErrorEventArgs args;
    args.message = message;
    args.errorCode = code;
    onError.notify(args);
}

} // namespace trussc
