#pragma once

// =============================================================================
// tcUdpSocket.h - UDP socket
// =============================================================================

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"
#include "../utils/tcLog.h"

// Platform-specific socket type
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketHandle = SOCKET;
    #define INVALID_SOCKET_HANDLE INVALID_SOCKET
#else
    using SocketHandle = int;
    #define INVALID_SOCKET_HANDLE (-1)
#endif

namespace trussc {

// ---------------------------------------------------------------------------
// UDP receive event arguments
// ---------------------------------------------------------------------------
struct UdpReceiveEventArgs {
    std::vector<char> data;     // Received data
    std::string remoteHost;     // Source host
    int remotePort = 0;         // Source port
};

// ---------------------------------------------------------------------------
// UDP error event arguments
// ---------------------------------------------------------------------------
struct UdpErrorEventArgs {
    std::string message;        // Error message
    int errorCode = 0;          // Error code
};

// ---------------------------------------------------------------------------
// UdpSocket - UDP socket class
// ---------------------------------------------------------------------------
class UdpSocket {
public:
    // Events
    Event<UdpReceiveEventArgs> onReceive;   // On data receive
    Event<UdpErrorEventArgs> onError;       // On error

    UdpSocket();
    ~UdpSocket();

    // Copy prohibited
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Move
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------

    // Create socket (usually not needed as bind/connect auto-create)
    bool create();

    // Bind port for receiving
    // startReceiving=true automatically starts receive thread
    bool bind(int port, bool startReceiving = true);

    // Set destination (after setting, can send via send(data, size))
    bool connect(const std::string& host, int port);

    // Close socket
    void close();

    // -------------------------------------------------------------------------
    // Send/Receive
    // -------------------------------------------------------------------------

    // Send to specified host and port
    bool sendTo(const std::string& host, int port, const void* data, size_t size);
    bool sendTo(const std::string& host, int port, const std::string& message);

    // Send to destination set by connect()
    bool send(const void* data, size_t size);
    bool send(const std::string& message);

    // Synchronous receive (blocking) - for when not using events
    // Returns: received byte count, -1 on error
    int receive(void* buffer, size_t bufferSize);
    int receive(void* buffer, size_t bufferSize, std::string& remoteHost, int& remotePort);

    // -------------------------------------------------------------------------
    // Receive thread control
    // -------------------------------------------------------------------------

    // Start receive thread (called automatically after bind)
    void startReceiving();

    // Stop receive thread
    void stopReceiving();

    // Whether receiving
    bool isReceiving() const { return receiving_.load(); }

    // -------------------------------------------------------------------------
    // Settings/Information
    // -------------------------------------------------------------------------

    // Set non-blocking mode
    bool setNonBlocking(bool nonBlocking);

    // Allow broadcast
    bool setBroadcast(bool enable);

    // Allow reuse (set before bind)
    bool setReuseAddress(bool enable);

    // Set receive buffer size
    bool setReceiveBufferSize(int size);

    // Set send buffer size
    bool setSendBufferSize(int size);

    // Set receive timeout (milliseconds, 0=infinite)
    bool setReceiveTimeout(int timeoutMs);

    // Get bound port
    int getLocalPort() const { return localPort_; }

    // Whether socket is valid
    bool isValid() const { return socket_ != INVALID_SOCKET_HANDLE; }

    // Destination information
    const std::string& getConnectedHost() const { return connectedHost_; }
    int getConnectedPort() const { return connectedPort_; }

private:
    void receiveThreadFunc();
    bool ensureSocket();
    void notifyError(const std::string& message, int code = 0);

    SocketHandle socket_ = INVALID_SOCKET_HANDLE;
    int localPort_ = 0;
    std::string connectedHost_;
    int connectedPort_ = 0;

    // Receive thread
    std::thread receiveThread_;
    std::atomic<bool> receiving_{false};
    std::atomic<bool> shouldStop_{false};

    // Receive buffer size
    static constexpr size_t RECEIVE_BUFFER_SIZE = 65536;

    // Winsock initialization (Windows)
    static bool initWinsock();
    static bool winsockInitialized_;
};

} // namespace trussc

namespace tc = trussc;
