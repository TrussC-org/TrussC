// =============================================================================
// tcUdpSocket.cpp - UDP socket implementation
// =============================================================================

#include "tc/network/tcUdpSocket.h"
#include <mutex>
#include <thread>
#include "tc/network/tcSocketInternal.h"

#include <chrono>
#include <cstring>
#include <new>

#ifdef _WIN32
#include <windows.h>
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
    #include <poll.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
#endif

#include "tc/events/tcCoreEvents.h"

namespace trussc {
namespace { thread_local const void* udpWorkerState = nullptr; }

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------




// ---------------------------------------------------------------------------
// Move operations
// ---------------------------------------------------------------------------




// ---------------------------------------------------------------------------
// Socket creation
// ---------------------------------------------------------------------------
bool UdpSocket::State::create() {
    if (socket_->handle != INVALID_SOCKET_HANDLE) {
        return true;  // Already created
    }

    socket_->handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_->handle == INVALID_SOCKET_HANDLE) {
        notifyError("Failed to create socket", SOCKET_ERROR_CODE);
        return false;
    }

    return true;
}

bool UdpSocket::State::ensureSocket() {
    if (socket_->closed) return false;   // close() shut it down; the handle has a fresh one
    if (socket_->handle == INVALID_SOCKET_HANDLE) {
        return create();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Bind
// ---------------------------------------------------------------------------
bool UdpSocket::State::bind(int port, bool startReceiving) {
    if (!ensureSocket()) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

#ifdef _WIN32
    // POSIX already refuses a second bind unless both sockets ask for reuse.
    // Windows lets a later SO_REUSEADDR socket take the port unless the first
    // one sets SO_EXCLUSIVEADDRUSE.
    if (!reuseAddress_ && !reusePort_) {
        int exclusive = 1;
        if (setsockopt(socket_->handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0) {
            notifyError("Failed to make port " + std::to_string(port) + " exclusive",
                        SOCKET_ERROR_CODE);
            return false;
        }
    }
#endif

    if (::bind(socket_->handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        notifyError("Failed to bind to port " + std::to_string(port), SOCKET_ERROR_CODE);
        return false;
    }

    sockaddr_in boundAddress{};
#ifdef _WIN32
    int boundSize = sizeof(boundAddress);
#else
    socklen_t boundSize = sizeof(boundAddress);
#endif
    if (getsockname(socket_->handle, reinterpret_cast<sockaddr*>(&boundAddress), &boundSize) != 0) {
        notifyError("Failed to get bound port", SOCKET_ERROR_CODE);
        return false;
    }
    localPort_ = ntohs(boundAddress.sin_port);
    bound_ = true;
    logNotice() << "UDP socket bound to port " << port;

    if (startReceiving) {
        return this->startReceiving();
    }

    return true;
}

// ---------------------------------------------------------------------------
// Set destination
// ---------------------------------------------------------------------------
bool UdpSocket::State::connect(const std::string& host, int port) {
    if (!ensureSocket()) {
        return false;
    }

    // Resolve hostname
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (status != 0 || !result) {
        notifyError(ErrorKind::Resolve, "Failed to resolve host: " + host, status);
        return false;
    }

    notifyRecovery(ErrorKind::Resolve);

    // Set destination with connect() (for UDP, this just fixes the destination for send(), not an actual connection)
    if (::connect(socket_->handle, result->ai_addr, static_cast<int>(result->ai_addrlen)) < 0) {
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
UdpSocket::Socket::~Socket() {
    if (handle != INVALID_SOCKET_HANDLE) CLOSE_SOCKET(handle);
}

void UdpSocket::State::close() {
    stopReceiving();
    // Close the descriptor now, as before the per-run State. shutdown() wakes
    // a blocked recvfrom on Linux, but on macOS/BSD an unconnected UDP socket
    // only wakes on close (shutdown fails with ENOTCONN). A direct receive()
    // blocked on it returns with an error; later calls see the invalid handle.
    std::lock_guard<std::mutex> lock(socket_->mutex);
    socket_->closed = true;
    const SocketHandle handle = socket_->handle.exchange(INVALID_SOCKET_HANDLE);
    if (handle != INVALID_SOCKET_HANDLE) {
#ifdef _WIN32
        shutdown(handle, SD_BOTH);
#else
        shutdown(handle, SHUT_RDWR);
#endif
        CLOSE_SOCKET(handle);
    }
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------
bool UdpSocket::State::sendTo(const std::string& host, int port, const void* data, size_t size) {
    if (!ensureSocket()) {
        return false;
    }

    // Resolve hostname
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (status != 0 || !result) {
        notifyError(ErrorKind::Resolve, "Failed to resolve host: " + host, status);
        return false;
    }

    notifyRecovery(ErrorKind::Resolve);

    auto sent = sendto(socket_->handle,
                       static_cast<const char*>(data),
                       static_cast<int>(size),
                       0,
                       result->ai_addr,
                       static_cast<int>(result->ai_addrlen));

    freeaddrinfo(result);

    if (sent < 0) {
        notifyError(ErrorKind::Send, "Failed to send data", SOCKET_ERROR_CODE);
        return false;
    }

    notifyRecovery(ErrorKind::Send);
    return true;
}

bool UdpSocket::State::sendTo(const std::string& host, int port, const std::string& message) {
    return sendTo(host, port, message.data(), message.size());
}

bool UdpSocket::State::send(const void* data, size_t size) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) {
        notifyError(ErrorKind::Send, "Socket not created");
        return false;
    }

    if (connectedHost_.empty()) {
        notifyError(ErrorKind::Send, "No destination set. Call connect() first.");
        return false;
    }

    auto sent = ::send(socket_->handle, static_cast<const char*>(data), static_cast<int>(size), 0);
    if (sent < 0) {
        notifyError(ErrorKind::Send, "Failed to send data", SOCKET_ERROR_CODE);
        return false;
    }

    notifyRecovery(ErrorKind::Send);
    return true;
}

bool UdpSocket::State::send(const std::string& message) {
    return send(message.data(), message.size());
}

// ---------------------------------------------------------------------------
// Receive (synchronous)
// ---------------------------------------------------------------------------
int UdpSocket::State::receive(void* buffer, size_t bufferSize) {
    std::string host;
    int port;
    return receive(buffer, bufferSize, host, port);
}

int UdpSocket::State::receive(void* buffer, size_t bufferSize, std::string& remoteHost, int& remotePort) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) {
        return -1;
    }

    sockaddr_in fromAddr{};
    socklen_t fromLen = sizeof(fromAddr);

    auto received = recvfrom(socket_->handle,
                             static_cast<char*>(buffer),
                             static_cast<int>(bufferSize),
                             0,
                             reinterpret_cast<sockaddr*>(&fromAddr),
                             &fromLen);

    if (received >= 0) notifyRecovery(ErrorKind::Receive);

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
bool UdpSocket::State::startReceiving() {
    if (socket_->handle == INVALID_SOCKET_HANDLE) return false;
    if (receiving_.load()) {
        return true;  // Already receiving
    }

    // A receive thread that a listener's stopReceiving() or close() let go
    // of has been told to stop: wait for it before clearing shouldStop_ for
    // the new one. The calling thread itself, if kept, stays.
    keptThreads_.joinOthers();

    // Every run has its own State; never resize a retired run's live buffer.
    try {
        if (receiveBuffer_.empty()) receiveBuffer_.resize(RECEIVE_BUFFER_SIZE);
    } catch (const std::bad_alloc&) {
        notifyError("Failed to allocate UDP receive buffer");
        return false;
    }
    shouldStop_ = false;
    receiving_ = true;

    if (useThread_) {
        // Non-blocking recv avoids a spurious readiness wake blocking teardown.
        setNonBlocking(true);
        keptThreads_.start(receiveThread_, [state = shared_from_this()] { state->receiveThreadFunc(); });
    } else {
        // Ensure non-blocking mode for event loop
        setNonBlocking(true);
        updateListener_ = events().update.listen([state = shared_from_this()] { state->processNetwork(); }, EventPriority::BeforeApp);
    }
    return true;
}

void UdpSocket::State::stopReceiving() {
    shouldStop_ = true;
    receiving_ = false;
    updateListener_.disconnect();
    // The non-blocking worker checks shouldStop_ after each bounded poll.
    // On its own thread keep the join for a later caller.
    keptThreads_.release(receiveThread_);
    keptThreads_.joinOthers();
    // Thread mode previously used blocking IO. Preserve what a direct receive()
    // observes after stopping, including a stop called from a receive listener.
    if (useThread_) {
        std::lock_guard<std::mutex> lock(socket_->mutex);
        setNonBlocking(false);
    }
}

void UdpSocket::State::processNetwork() {
    if (!receiving_.load()) return;

    auto& buffer = receiveBuffer_;

    // In event loop mode, we poll first or just try recvfrom in non-blocking mode.
    // Since we set non-blocking in startReceiving, we can just try recvfrom.
    // Loop until WouldBlock
    
    while (receiving_.load()) {
        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);

        std::unique_lock<std::mutex> socketLock(socket_->mutex);
        if (shouldStop_ || socket_->handle == INVALID_SOCKET_HANDLE) break;
        auto received = recvfrom(socket_->handle,
                                 buffer.data(),
                                 static_cast<int>(buffer.size()),
                                 0,
                                 reinterpret_cast<sockaddr*>(&fromAddr),
                                 &fromLen);

        socketLock.unlock();
        if (received >= 0) notifyRecovery(ErrorKind::Receive);

        if (received > 0) {
            UdpReceiveEventArgs args;
            args.data.assign(buffer.begin(), buffer.begin() + received);

            char hostStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
            args.remoteHost = hostStr;
            args.remotePort = ntohs(fromAddr.sin_port);

            onReceive.notifyExternal(args);
            
            // If not threaded, we process one packet per update to avoid stalling?
            // Or process all pending? All pending is better for latency.
            // But limit loop count to prevent infinite loop if data comes faster than process.
            // For now, process all available.
        } else {
            int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (received < 0 && (err == WSAEWOULDBLOCK || err == WSAEINTR)) break;
#else
            if (received < 0 && (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)) break;
#endif
            // Check for actual error
            if (received < 0) {
                if (!shouldStop_.load()) {
                    notifyError(ErrorKind::Receive, "Receive error", err);
                }
                // Don't stop receiving on error, just log and wait for next
                break; 
            }
            // received == 0 (should not happen for UDP usually, but break if it does)
            break;
        }
    }
}

void UdpSocket::State::receiveThreadFunc() {
    udpWorkerState = this;
    // In thread mode, we use blocking IO or select/poll
    // Re-implementing with select/poll to allow proper stopping
    
    auto& buffer = receiveBuffer_;
    bool waitFailing = false;   // the previous wait failed too (report once per run)

    while (!shouldStop_.load()) {
        // Poll with timeout to allow checking shouldStop
        bool dataReady = false;
        
#ifdef _WIN32
        const SocketHandle descriptor = socket_->handle.load();
        if (descriptor == INVALID_SOCKET_HANDLE) break;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(descriptor, &readfds);
        timeval tv = {0, 100000}; // 100ms
        int res = select(0, &readfds, NULL, NULL, &tv);
        if (res > 0) dataReady = true;
#else
        struct pollfd pfd;
        pfd.fd = socket_->handle;
        pfd.events = POLLIN;
        int res = poll(&pfd, 1, 100); // 100ms
        if (res > 0) dataReady = true;
#endif

        if (shouldStop_.load()) break;

        // A failed wait returns at once, so looping straight back into it
        // would spin a core at 100% for as long as the failure lasts (Winsock
        // torn down under the socket, for one), silently, while isReceiving()
        // still says true. Report the first failure of a run and back off for
        // one wait slice before trying again.
        if (res < 0) {
            int err = SOCKET_ERROR_CODE;
#ifndef _WIN32
            if (err == EINTR) continue;   // interrupted by a signal, not a failure
#endif
            if (!waitFailing) {
                notifyError(ErrorKind::Receive, "Receive wait failed", err);
                waitFailing = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        waitFailing = false;

        if (dataReady) {
            sockaddr_in fromAddr{};
            socklen_t fromLen = sizeof(fromAddr);

            std::unique_lock<std::mutex> socketLock(socket_->mutex);
            if (shouldStop_ || socket_->handle == INVALID_SOCKET_HANDLE) break;
            auto received = recvfrom(socket_->handle,
                                     buffer.data(),
                                     static_cast<int>(buffer.size()),
                                     0,
                                     reinterpret_cast<sockaddr*>(&fromAddr),
                                     &fromLen);

            socketLock.unlock();
            if (shouldStop_.load()) break;

            if (received >= 0) notifyRecovery(ErrorKind::Receive);

            if (received > 0) {
                UdpReceiveEventArgs args;
                args.data.assign(buffer.begin(), buffer.begin() + received);

                char hostStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &fromAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
                args.remoteHost = hostStr;
                args.remotePort = ntohs(fromAddr.sin_port);

                onReceive.notifyExternal(args);
            } else if (received < 0) {
                int err = SOCKET_ERROR_CODE;
                // Ignore wouldblock in case of spurious wakeup
#ifdef _WIN32
                if (err != WSAEWOULDBLOCK && err != WSAEINTR)
#else
                if (err != EAGAIN && err != EWOULDBLOCK && err != EINTR)
#endif
                {
                    notifyError(ErrorKind::Receive, "Receive error", err);
                }
            }
        }
    }

    receiving_ = false;
}

void UdpSocket::State::setUseThread(bool use) {
#ifdef __EMSCRIPTEN__
    if (use) {
        logWarning() << "Threads are not supported on Emscripten in this build. useThread remains false.";
        return;
    }
#endif
    if (receiving_) {
        logWarning() << "Cannot change threading mode while receiving. Stop receiving first.";
        return;
    }
    useThread_ = use;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
bool UdpSocket::State::setNonBlocking(bool nonBlocking) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) return false;

#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    return ioctlsocket(socket_->handle, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_->handle, F_GETFL, 0);
    if (flags < 0) return false;
    if (nonBlocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(socket_->handle, F_SETFL, flags) == 0;
#endif
}

bool UdpSocket::State::setBroadcast(bool enable) {
    if (!ensureSocket()) return false;
    int val = enable ? 1 : 0;
    return setsockopt(socket_->handle, SOL_SOCKET, SO_BROADCAST,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool UdpSocket::State::setReuseAddress(bool enable) {
    if (!ensureSocket()) return false;
    int val = enable ? 1 : 0;
    if (setsockopt(socket_->handle, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&val), sizeof(val)) != 0) {
        notifyError("Failed to set address reuse", SOCKET_ERROR_CODE);
        return false;
    }
    reuseAddress_ = enable;
    return true;
}

// ---------------------------------------------------------------------------
// Multicast (IPv4)
// ---------------------------------------------------------------------------
namespace {
// Fill an ip_mreq from a group address and an optional interface address.
// Returns false if either address fails to parse.
bool buildMreq(const std::string& groupAddr, const std::string& interfaceAddr, ip_mreq& mreq) {
    mreq = ip_mreq{};
    if (inet_pton(AF_INET, groupAddr.c_str(), &mreq.imr_multiaddr) != 1) {
        return false;
    }
    if (interfaceAddr.empty()) {
        mreq.imr_interface.s_addr = INADDR_ANY;  // default route
    } else if (inet_pton(AF_INET, interfaceAddr.c_str(), &mreq.imr_interface) != 1) {
        return false;
    }
    return true;
}
} // namespace

bool UdpSocket::State::joinMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr) {
    if (!ensureSocket()) return false;
    ip_mreq mreq;
    if (!buildMreq(groupAddr, interfaceAddr, mreq)) {
        notifyError("Invalid multicast/interface address: " + groupAddr, 0);
        return false;
    }
    if (setsockopt(socket_->handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0) {
        notifyError("Failed to join multicast group " + groupAddr, SOCKET_ERROR_CODE);
        return false;
    }
    return true;
}

bool UdpSocket::State::leaveMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) return false;
    ip_mreq mreq;
    if (!buildMreq(groupAddr, interfaceAddr, mreq)) {
        notifyError("Invalid multicast/interface address: " + groupAddr, 0);
        return false;
    }
    return setsockopt(socket_->handle, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                      reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == 0;
}

bool UdpSocket::State::setMulticastTTL(int ttl) {
    if (!ensureSocket()) return false;
    // IP_MULTICAST_TTL expects an unsigned char payload on POSIX; Winsock takes
    // an int. A single byte satisfies both (TTL is 0..255 anyway).
    unsigned char val = static_cast<unsigned char>(ttl);
    return setsockopt(socket_->handle, IPPROTO_IP, IP_MULTICAST_TTL,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool UdpSocket::State::setMulticastLoopback(bool enable) {
    if (!ensureSocket()) return false;
    unsigned char val = enable ? 1 : 0;
    return setsockopt(socket_->handle, IPPROTO_IP, IP_MULTICAST_LOOP,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool UdpSocket::State::setMulticastInterface(const std::string& interfaceAddr) {
    if (!ensureSocket()) return false;
    struct in_addr ifaddr{};
    if (interfaceAddr.empty()) {
        ifaddr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, interfaceAddr.c_str(), &ifaddr) != 1) {
        notifyError("Invalid multicast interface address: " + interfaceAddr, 0);
        return false;
    }
    return setsockopt(socket_->handle, IPPROTO_IP, IP_MULTICAST_IF,
                      reinterpret_cast<const char*>(&ifaddr), sizeof(ifaddr)) == 0;
}

bool UdpSocket::State::setReusePort(bool enable) {
    if (!ensureSocket()) return false;
    int val = enable ? 1 : 0;
#ifdef _WIN32
    // Windows has no SO_REUSEPORT; SO_REUSEADDR already permits multiple binds.
    const int option = SO_REUSEADDR;
#else
    const int option = SO_REUSEPORT;
#endif
    if (setsockopt(socket_->handle, SOL_SOCKET, option,
                   reinterpret_cast<const char*>(&val), sizeof(val)) != 0) {
        notifyError("Failed to set port reuse", SOCKET_ERROR_CODE);
        return false;
    }
    reusePort_ = enable;
    return true;
}

bool UdpSocket::State::setReceiveBufferSize(int size) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) return false;
    // Bound (security): untrusted datagram floods can fill the OS queue; the configured buffer keeps queued memory finite. Overflow is handled by the OS dropping datagrams.
    return setsockopt(socket_->handle, SOL_SOCKET, SO_RCVBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size)) == 0;
}

bool UdpSocket::State::setSendBufferSize(int size) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) return false;
    return setsockopt(socket_->handle, SOL_SOCKET, SO_SNDBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size)) == 0;
}

bool UdpSocket::State::setReceiveTimeout(int timeoutMs) {
    if (socket_->handle == INVALID_SOCKET_HANDLE) return false;

#ifdef _WIN32
    DWORD tv = timeoutMs;
    return setsockopt(socket_->handle, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(socket_->handle, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Error notification
// ---------------------------------------------------------------------------
// Gate, counter and failure flag are atomic so a suppressed failure takes no mutex. Only
// reports/recoveries take the small bookkeeping lock. Emit outside it: logger
// listeners and onError listeners may call back into this socket.
void UdpSocket::State::notifyError(ErrorKind kind, const std::string& message, int code) {
    auto& state = errorLogs_[static_cast<size_t>(kind)];
    const bool report = state.gate.isFirstTime();
    uint64_t count = 0;
    if (report) {
        std::lock_guard<std::mutex> lock(errorLogMutex_);
        count = state.suppressed.exchange(0);
    } else {
        state.suppressed.fetch_add(1);
    }
    // Publish AFTER touching the gate/counter. Recovery clears this flag BEFORE
    // resetting the gate: a racing failure is either covered by that reset, or
    // publishes true after the clear so the next success resets again. Publishing
    // before the gate check could let a failure consume the reset with no flag
    // left to re-arm it. Release/acquire orders bookkeeping across threads.
    state.failedSinceSuccess.store(true, std::memory_order_release);
    if (report) {
        auto line = logError();
        line << "UdpSocket: " << message << " (code: " << code << ")";
        if (count > 0) line << " (+" << count << " more since the last report)";
    }

    UdpErrorEventArgs args;
    args.message = message;
    args.errorCode = code;
    if (kind == ErrorKind::Receive) onError.notifyExternal(args);
    else onError.notify(args);
}

void UdpSocket::State::notifyRecovery(ErrorKind kind) {
    auto& state = errorLogs_[static_cast<size_t>(kind)];
    if (!state.failedSinceSuccess.load(std::memory_order_acquire)) return;

    uint64_t count;
    {
        std::lock_guard<std::mutex> lock(errorLogMutex_);
        if (!state.failedSinceSuccess.exchange(false, std::memory_order_acq_rel)) return;
        count = state.suppressed.exchange(0);
        state.gate.reset();
    }
    if (count > 0) {
        const char* name = kind == ErrorKind::Resolve ? "resolve" :
                           kind == ErrorKind::Send ? "send" : "receive";
        logNotice() << "UdpSocket: " << name << " recovered after " << count << " more failures";
    }
}

void UdpSocket::State::notifyError(const std::string& message, int code) {
    logError() << "UdpSocket: " << message << " (code: " << code << ")";

    UdpErrorEventArgs args;
    args.message = message;
    args.errorCode = code;
    onError.notify(args);
}



void UdpSocket::freshState() {
    auto previous = internal::sharedLoad(state_);
    socketOwner_ = std::make_shared<Socket>();
    auto next = std::make_shared<State>(socketOwner_);
    next->useThread_ = previous->useThread_;
    next->onReceive.target = onReceiveTarget_;
    next->onError.target = onErrorTarget_;
    retired_.push_back(previous);
    internal::sharedStore(state_, std::move(next));
}
UdpSocket::UdpSocket(UdpSocket&& other) noexcept : UdpSocket() { *this = std::move(other); }
UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this == &other) return *this;
    close();
    if (other.isReceiving()) {
        logError("UdpSocket") << "moved while running; stopped";
        other.stopReceiving();
    }
    socketOwner_ = other.socketOwner_;
    internal::sharedStore(state_, internal::sharedLoad(other.state_));
    internal::sharedLoad(state_)->onReceive.target = onReceiveTarget_;
    internal::sharedLoad(state_)->onError.target = onErrorTarget_;
    internal::sharedStore(other.state_, std::make_shared<State>());
    other.freshState();
    return *this;
}


void UdpSocket::joinRetired() {
    if (udpWorkerState) {
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (it->use_count() == 1) {
                (*it)->keptThreads_.joinOthers();
                it = retired_.erase(it);
            } else ++it;
        }
        return;
    }
    auto retired = std::move(retired_);
    for (auto& state : retired) state->keptThreads_.joinOthers();
}

void UdpSocket::attachTargets() {
    onReceiveTarget_ = internal::EventSnapshotDelivery<UdpReceiveEventArgs>::bind(onReceive, delivery_);
    internal::sharedLoad(state_)->onReceive.target = onReceiveTarget_;
    onErrorTarget_ = internal::EventSnapshotDelivery<UdpErrorEventArgs>::bind(onError, delivery_);
    internal::sharedLoad(state_)->onError.target = onErrorTarget_;
}
UdpSocket::UdpSocket() { internal::ensureWinsock(); attachTargets(); }
UdpSocket::~UdpSocket() {
    delivery_->close();
    onReceive.clear();
    onError.clear();

    onReceiveTarget_.reset();
    onErrorTarget_.reset();
    auto state = internal::sharedLoad(state_);
    state->close();
    joinRetired();
}
bool UdpSocket::create() {
    auto state = internal::sharedLoad(state_);
    return state->create();
}
bool UdpSocket::bind(int port, bool startReceiving) {
    auto state = internal::sharedLoad(state_);
    return state->bind(port, startReceiving);
}
bool UdpSocket::connect(const std::string& host, int port) {
    auto state = internal::sharedLoad(state_);
    return state->connect(host, port);
}
void UdpSocket::close() {
    auto state = internal::sharedLoad(state_);
    state->close();
    freshState();
    joinRetired();
}
bool UdpSocket::sendTo(const std::string& host, int port, const void* data, size_t size) {
    auto state = internal::sharedLoad(state_);
    return state->sendTo(host, port, data, size);
}
bool UdpSocket::sendTo(const std::string& host, int port, const std::string& message) {
    auto state = internal::sharedLoad(state_);
    return state->sendTo(host, port, message);
}
bool UdpSocket::send(const void* data, size_t size) {
    auto state = internal::sharedLoad(state_);
    return state->send(data, size);
}
bool UdpSocket::send(const std::string& message) {
    auto state = internal::sharedLoad(state_);
    return state->send(message);
}
int UdpSocket::receive(void* buffer, size_t bufferSize) {
    auto state = internal::sharedLoad(state_);
    return state->receive(buffer, bufferSize);
}
int UdpSocket::receive(void* buffer, size_t bufferSize, std::string& remoteHost, int& remotePort) {
    auto state = internal::sharedLoad(state_);
    return state->receive(buffer, bufferSize, remoteHost, remotePort);
}
void UdpSocket::startReceiving() {
    joinRetired();
    auto state = internal::sharedLoad(state_);
    state->startReceiving();
}
void UdpSocket::stopReceiving() {
    auto state = internal::sharedLoad(state_);
    const int port = state->localPort_;
    const bool bound = state->bound_;
    const auto host = state->connectedHost_;
    const int destination = state->connectedPort_;
    const bool reuseAddress = state->reuseAddress_;
    const bool reusePort = state->reusePort_;
    state->stopReceiving();
    auto next = std::make_shared<State>(state->socket_);
    next->useThread_ = state->useThread_;
    next->onReceive.target = onReceiveTarget_;
    next->onError.target = onErrorTarget_;
    next->localPort_ = port;
    next->bound_ = bound;
    next->connectedHost_ = host;
    next->connectedPort_ = destination;
    next->reuseAddress_ = reuseAddress;
    next->reusePort_ = reusePort;
    retired_.push_back(state);
    internal::sharedStore(state_, std::move(next));
    joinRetired();
}
void UdpSocket::processNetwork() {
    auto state = internal::sharedLoad(state_);
    state->processNetwork();
}
void UdpSocket::setUseThread(bool use) {
    auto state = internal::sharedLoad(state_);
    state->setUseThread(use);
}
bool UdpSocket::setNonBlocking(bool nonBlocking) {
    auto state = internal::sharedLoad(state_);
    return state->setNonBlocking(nonBlocking);
}
bool UdpSocket::setBroadcast(bool enable) {
    auto state = internal::sharedLoad(state_);
    return state->setBroadcast(enable);
}
bool UdpSocket::setReuseAddress(bool enable) {
    auto state = internal::sharedLoad(state_);
    return state->setReuseAddress(enable);
}
bool UdpSocket::joinMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr) {
    auto state = internal::sharedLoad(state_);
    return state->joinMulticastGroup(groupAddr, interfaceAddr);
}
bool UdpSocket::leaveMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr) {
    auto state = internal::sharedLoad(state_);
    return state->leaveMulticastGroup(groupAddr, interfaceAddr);
}
bool UdpSocket::setMulticastTTL(int ttl) {
    auto state = internal::sharedLoad(state_);
    return state->setMulticastTTL(ttl);
}
bool UdpSocket::setMulticastLoopback(bool enable) {
    auto state = internal::sharedLoad(state_);
    return state->setMulticastLoopback(enable);
}
bool UdpSocket::setMulticastInterface(const std::string& interfaceAddr) {
    auto state = internal::sharedLoad(state_);
    return state->setMulticastInterface(interfaceAddr);
}
bool UdpSocket::setReusePort(bool enable) {
    auto state = internal::sharedLoad(state_);
    return state->setReusePort(enable);
}
bool UdpSocket::setReceiveBufferSize(int size) {
    auto state = internal::sharedLoad(state_);
    return state->setReceiveBufferSize(size);
}
bool UdpSocket::setSendBufferSize(int size) {
    auto state = internal::sharedLoad(state_);
    return state->setSendBufferSize(size);
}
bool UdpSocket::setReceiveTimeout(int timeoutMs) {
    auto state = internal::sharedLoad(state_);
    return state->setReceiveTimeout(timeoutMs);
}
void UdpSocket::notifyError(ErrorKind kind, const std::string& message, int code) {
    auto state = internal::sharedLoad(state_);
    state->notifyError(kind, message, code);
}
void UdpSocket::notifyRecovery(ErrorKind kind) {
    auto state = internal::sharedLoad(state_);
    state->notifyRecovery(kind);
}
void UdpSocket::notifyError(const std::string& message, int code) {
    auto state = internal::sharedLoad(state_);
    state->notifyError(message, code);
}

} // namespace trussc
