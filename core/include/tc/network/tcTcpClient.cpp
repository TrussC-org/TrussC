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
    if (connected_ || running_ || connectPending_) {
        disconnect();
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
        // Clean up before notifying: an onError listener that reconnects
        // must not have its new socket closed after it returns
        CLOSE_SOCKET(socket_);
#ifdef _WIN32
        socket_ = INVALID_SOCKET;
#else
        socket_ = -1;
#endif
        notifyError("Failed to resolve host: " + host, ret);
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
            // Clean up before notifying (see above)
            CLOSE_SOCKET(socket_);
#ifdef _WIN32
            socket_ = INVALID_SOCKET;
#else
            socket_ = -1;
#endif
            notifyError("Failed to connect to " + host + ":" + std::to_string(port), err);
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
            receiveThread_ = std::thread(&TcpClient::receiveThreadFunc, this, generation,
                                         alive_);
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
    // An onError listener may have started a newer attempt from inside the
    // connect() above. That attempt reports its own result: this one is not
    // reported once the client is connected or connecting again.
    if (!success && !connected_ && !running_ && !connectPending_) {
        TcpConnectEventArgs args;
        args.success = false;
        args.message = "Connection failed";
        onConnect.notify(args);
    }
}

void TcpClient::disconnect() {
    disconnectImpl(true);
}

// disconnect() with notify, the destructor without
void TcpClient::disconnectImpl(bool notify) {
    running_ = false;
    connectPending_ = false;
    updateListener_.disconnect();

    resetConnection();

    // On the connect thread itself (a listener there), keep it for a later
    // join from another thread. Then join what earlier calls kept.
    keptThreads_.release(connectThread_);
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
    // Without threads (driven by the update event) the result is not needed:
    // a stop means the connection ended and the update listener is gone.
    // The token is copied first: a listener may destroy the client.
    AliveToken alive = alive_;
    processNetworkStep(alive);
}

// processNetwork()'s work. Returns false when the caller must stop without
// reading the client again (see the header).
bool TcpClient::processNetworkStep(const AliveToken& alive) {
    if (!running_) return true;

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
                // Tear down before notifying: a listener that reconnects must
                // not have its new connection torn down after it returns
                disconnect();
                notifyError("Connection failed", err);
                // An onError listener may have destroyed the client
                if (!*alive) return false;
                // Not reported if an onError listener started a newer attempt
                if (!connected_ && !running_ && !connectPending_) {
                    TcpConnectEventArgs args;
                    args.success = false;
                    args.message = "Connection failed";
                    onConnect.notify(args);
                }
                return false;
            }
            if (FD_ISSET(socket_, &writefds)) {
                connectPending_ = false;
                connected_ = true;
                logNotice() << "TCP connected (async) to " << remoteHost_ << ":" << remotePort_;
                TcpConnectEventArgs args;
                args.success = true;
                args.message = "Connected";
                onConnect.notify(args);
                // An onConnect listener may have destroyed the client
                if (!*alive) return false;
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
                // An onConnect listener may have destroyed the client
                if (!*alive) return false;
            } else {
                // Tear down before notifying (see the Windows branch)
                disconnect();
                notifyError("Connection failed", err);
                // An onError listener may have destroyed the client
                if (!*alive) return false;
                // Not reported if an onError listener started a newer attempt
                if (!connected_ && !running_ && !connectPending_) {
                    TcpConnectEventArgs args;
                    args.success = false;
                    args.message = "Connection failed";
                    onConnect.notify(args);
                }
                return false;
            }
        }
#endif
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
        int received = static_cast<int>(recv(socket_, recvBuf_.data(), recvBuf_.size(), 0));

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
            int err = SOCKET_ERROR_CODE;
            if (err == WOULD_BLOCK_ERROR) break;
            
            // As above: an error caused by a local disconnect() is its to report
            if (running_.exchange(false)) {
                connected_ = false;
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
