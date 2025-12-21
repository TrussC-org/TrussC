// =============================================================================
// tcTcpServer.h - TCP server socket
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <functional>
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
#endif

namespace trussc {

// =============================================================================
// Connected client information
// =============================================================================
struct TcpServerClient {
    int id;                 // Client ID (assigned by server)
    std::string host;       // Client IP address
    int port;               // Client port

#ifdef _WIN32
    SOCKET socket;
#else
    int socket;
#endif
};

// =============================================================================
// Event arguments
// =============================================================================

// Client connect event
struct TcpClientConnectEventArgs {
    int clientId;
    std::string host;
    int port;
};

// Data receive from client event
struct TcpServerReceiveEventArgs {
    int clientId;
    std::vector<char> data;
};

// Client disconnect event
struct TcpClientDisconnectEventArgs {
    int clientId;
    std::string reason;
    bool wasClean;
};

// Server error event
struct TcpServerErrorEventArgs {
    std::string message;
    int errorCode = 0;
    int clientId = -1;  // -1 = server itself error
};

// =============================================================================
// TcpServer class
// =============================================================================
class TcpServer {
public:
    // -------------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------------
    Event<TcpClientConnectEventArgs> onClientConnect;       // On client connect
    Event<TcpServerReceiveEventArgs> onReceive;             // On data receive
    Event<TcpClientDisconnectEventArgs> onClientDisconnect; // On client disconnect
    Event<TcpServerErrorEventArgs> onError;                 // On error

    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    TcpServer();
    ~TcpServer();

    // Copy prohibited
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // -------------------------------------------------------------------------
    // Server management
    // -------------------------------------------------------------------------

    // Start server (listen on specified port)
    bool start(int port, int maxClients = 10);

    // Stop server
    void stop();

    // Whether server is running
    bool isRunning() const;

    // -------------------------------------------------------------------------
    // Client management
    // -------------------------------------------------------------------------

    // Disconnect specified client
    void disconnectClient(int clientId);

    // Disconnect all clients
    void disconnectAllClients();

    // Number of connected clients
    int getClientCount() const;

    // Get all connected client IDs
    std::vector<int> getClientIds() const;

    // Get client information (nullptr if not found)
    const TcpServerClient* getClient(int clientId) const;

    // -------------------------------------------------------------------------
    // Data send
    // -------------------------------------------------------------------------

    // Send data to specified client
    bool send(int clientId, const void* data, size_t size);
    bool send(int clientId, const std::vector<char>& data);
    bool send(int clientId, const std::string& message);

    // Broadcast to all clients
    void broadcast(const void* data, size_t size);
    void broadcast(const std::vector<char>& data);
    void broadcast(const std::string& message);

    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------

    // Set receive buffer size
    void setReceiveBufferSize(size_t size);

    // -------------------------------------------------------------------------
    // Information retrieval
    // -------------------------------------------------------------------------

    // Listening port
    int getPort() const;

private:
    void acceptThreadFunc();
    void clientThreadFunc(int clientId);
    void notifyError(const std::string& msg, int code = 0, int clientId = -1);
    void removeClient(int clientId);

#ifdef _WIN32
    SOCKET serverSocket_ = INVALID_SOCKET;
#else
    int serverSocket_ = -1;
#endif

    int port_ = 0;
    int maxClients_ = 10;

    std::thread acceptThread_;
    std::atomic<bool> running_{false};

    std::unordered_map<int, TcpServerClient> clients_;
    std::unordered_map<int, std::thread> clientThreads_;
    mutable std::mutex clientsMutex_;

    int nextClientId_ = 1;
    size_t receiveBufferSize_ = 65536;

    static std::atomic<int> instanceCount_;
    static void initWinsock();
    static void cleanupWinsock();
};

} // namespace trussc

namespace tc = trussc;
