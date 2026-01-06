// =============================================================================
// tcTcpClient.h - TCP client socket
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
    #define WOULD_BLOCK_ERROR WSAEWOULDBLOCK
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
    #define WOULD_BLOCK_ERROR EWOULDBLOCK
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace trussc {

// =============================================================================
// Event arguments
// =============================================================================

// Connection complete event
struct TcpConnectEventArgs {
    bool success = false;
    std::string message;
};

// Data receive event
struct TcpReceiveEventArgs {
    std::vector<char> data;
};

// Disconnect event
struct TcpDisconnectEventArgs {
    std::string reason;
    bool wasClean = true;  // Whether it was a clean disconnect
};

// Error event
struct TcpErrorEventArgs {
    std::string message;
    int errorCode = 0;
};

// =============================================================================
// TcpClient class (base class - parent of TlsClient)
// =============================================================================
class TcpClient {
public:
    // -------------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------------
    Event<TcpConnectEventArgs> onConnect;       // On connection complete
    Event<TcpReceiveEventArgs> onReceive;       // On data receive
    Event<TcpDisconnectEventArgs> onDisconnect; // On disconnect
    Event<TcpErrorEventArgs> onError;           // On error

    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    TcpClient();
    virtual ~TcpClient();

    // Copy prohibited
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // Move allowed
    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;

    // -------------------------------------------------------------------------
    // Connection management (virtual - can be overridden in TlsClient)
    // -------------------------------------------------------------------------

    // Connect to server (blocking)
    virtual bool connect(const std::string& host, int port);

    // Connect to server asynchronously (connects in background, notifies via onConnect)
    virtual void connectAsync(const std::string& host, int port);

    // Disconnect
    virtual void disconnect();

    // Whether connected
    virtual bool isConnected() const;

    // -------------------------------------------------------------------------
    // Data send/receive (virtual - can be overridden in TlsClient)
    // -------------------------------------------------------------------------

    // Send data
    virtual bool send(const void* data, size_t size);
    virtual bool send(const std::vector<char>& data);
    virtual bool send(const std::string& message);

    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------

    // Set receive buffer size
    void setReceiveBufferSize(size_t size);

    // Set blocking mode
    void setBlocking(bool blocking);

    // Set whether to use threads (Wasm must be false)
    void setUseThread(bool useThread);

    // Whether threading is being used
    bool isUsingThread() const;

    // Internal update method (called by event listener if not using threads)
    virtual void processNetwork();

    // -------------------------------------------------------------------------
    // Information retrieval
    // -------------------------------------------------------------------------

    // Remote host name
    std::string getRemoteHost() const;

    // Remote port
    int getRemotePort() const;

protected:
    // Accessible from derived classes
    void notifyError(const std::string& msg, int code = 0);

#ifdef _WIN32
    SOCKET socket_ = INVALID_SOCKET;
#else
    int socket_ = -1;
#endif

    std::string remoteHost_;
    int remotePort_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    size_t receiveBufferSize_ = 65536;
    std::mutex sendMutex_;

#ifdef __EMSCRIPTEN__
    bool useThread_ = false;
#else
    bool useThread_ = true;
#endif
    EventListener updateListener_;
    bool connectPending_ = false;

private:
    void receiveThreadFunc();
    void connectThreadFunc(const std::string& host, int port);

    std::thread receiveThread_;
    std::thread connectThread_;

    static std::atomic<int> instanceCount_;
    static void initWinsock();
    static void cleanupWinsock();
};

} // namespace trussc

namespace tc = trussc;
