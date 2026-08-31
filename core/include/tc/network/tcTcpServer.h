// =============================================================================
// tcTcpServer.h - TCP server socket
// =============================================================================
#pragma once
#include "tc/utils/tcAnnotations.h"

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

namespace internal {

// -----------------------------------------------------------------------------
// Per-client send channel.
//
// Sending must NOT hold the server-wide client mutex: a client that stops
// reading would otherwise block client registration, disconnects and every
// other send (head-of-line blocking). Each client therefore owns its own send
// mutex, held by shared_ptr so an in-flight send keeps the channel alive even
// after the client is erased from the map.
//
// The socket is non-blocking and the send loop waits in short slices, so
// clearing `open` is what cuts a send short when the client goes away: nothing
// else can interrupt a send that is already parked (Winsock's shutdown() does
// not). It is cleared before the socket is shut down, so a send that wakes up
// mid-teardown fails instead of writing to a closed (or recycled) descriptor.
// -----------------------------------------------------------------------------
struct TcpSendChannel {
#ifdef _WIN32
    SOCKET socket = INVALID_SOCKET;
#else
    int socket = -1;
#endif
    std::mutex mutex;
    std::atomic<bool> open{true};
};

} // namespace internal

// =============================================================================
// Connected client information
// =============================================================================
class TcpServerClient {
public:
    int getId() const { return id_; }
    const std::string& getHost() const { return host_; }
    int getPort() const { return port_; }

private:
    friend class TcpServer;

    int id_;                // Client ID (assigned by server)
    std::string host_;      // Client IP address
    int port_;              // Client port

#ifdef _WIN32
    SOCKET socket_;
#else
    int socket_;
#endif

    // Owns the send mutex for this client (see internal::TcpSendChannel)
    std::shared_ptr<internal::TcpSendChannel> channel_;
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
class TC_PLATFORMS("macos,windows,linux,android") TcpServer {
public:
    // -------------------------------------------------------------------------
    // Events
    //
    // THREADING: TcpServer always runs internal accept/receive threads, so
    // these events fire on those threads, not the main thread. A listener that
    // touches the Node tree, GPU resources, or unguarded app state must opt
    // into main-thread delivery:
    //
    //   listener = server.onReceive.listen(fn, Deliver::Main);
    //
    // Deliver::Main copies the payload and runs the listener at the start of
    // the next frame (see tcEvent.h). Plain listen(fn) runs inline on the
    // firing thread — fastest, but you handle the synchronization.
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

    // Set how long a send may make NO progress before it gives up, in seconds
    // (0 = wait indefinitely). Defaults to 60 s: long enough that no healthy
    // peer trips it, short enough that a dead one cannot hang a send forever.
    // Applies to every send that starts after this call, on every client.
    void setSendTimeout(float seconds);

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
    std::atomic<float> sendTimeout_{60.0f};   // idle, not total; read by every sending thread

    // Look up a client's send channel without holding clientsMutex_ during the send
    std::shared_ptr<internal::TcpSendChannel> findChannel(int clientId) const;

    // Clear `open`, shut down and close the socket, in that order
    static void closeChannel(const std::shared_ptr<internal::TcpSendChannel>& ch);

    static std::atomic<int> instanceCount_;
    static void initWinsock();
    static void cleanupWinsock();
};

} // namespace trussc

namespace tc = trussc;
