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
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <shared_mutex>
#include <memory>
#include <unordered_map>
#include <functional>
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/network/tcSendResult.h"

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
// A synchronous send waiting on its queued payload.
//
// send() is sendAsync() plus this: it queues the payload like anything else and
// blocks here until the writer thread reports back. Sharing one queue is what
// keeps sync and async sends to the same client in order, on one code path,
// under one idle timeout.
// -----------------------------------------------------------------------------
struct TcpSendWaiter {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    SendError error = SendError::None;
    size_t bytesSent = 0;
};

// -----------------------------------------------------------------------------
// One queued payload.
//
// sendAsync() has to own its bytes, since it returns before they are written,
// and broadcastAsync() hands the same buffer to every client rather than
// copying a frame once per peer. send() blocks until the item completes, so its
// caller's buffer is still there: it lends the pointer instead of copying, and
// stays as cheap as it was before the queue existed.
// -----------------------------------------------------------------------------
struct TcpSendItem {
    std::shared_ptr<const std::vector<char>> owned;   // sendAsync / broadcastAsync
    const void* borrowed = nullptr;                   // send(), whose caller is blocked
    size_t size = 0;
    uint64_t id = 0;
    std::shared_ptr<TcpSendWaiter> waiter;            // set by send(), null otherwise

    const void* data() const { return owned ? static_cast<const void*>(owned->data()) : borrowed; }
};

// -----------------------------------------------------------------------------
// Per-client send channel.
//
// Sending must NOT hold the server-wide client mutex: a client that stops
// reading would otherwise block client registration, disconnects and every
// other send (head-of-line blocking). Each client therefore owns its own queue,
// held by shared_ptr so an in-flight send keeps the channel alive even after
// the client is erased from the map.
//
// One writer thread per client drains the queue. It is the only thread that
// ever touches the descriptor for writing, and it is the one that closes it on
// its way out, so a teardown can never pull the descriptor out from under a
// send in flight. `mutex` guards the queue and the descriptor, never the send
// itself: a slow peer must not stall the next sendAsync().
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
    std::condition_variable queued;   // writer waits here for work, or for the channel to close
    std::condition_variable room;     // send() waits here when the queue is at its mark
    std::deque<TcpSendItem> queue;
    size_t pendingBytes = 0;          // accepted and not yet completed
    std::thread::id writerId;         // set once by the writer; send() must not wait on itself
    std::atomic<bool> open{true};

    // The send path gave up on this client (a timeout truncated a payload).
    // The receive thread does the removal: the writer must never tear down the
    // client it is running for, or it would end up joining itself.
    std::atomic<bool> dropped{false};
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

    // A queued send finished, successfully or not. Fires on the writer thread
    // for that client — the same threading note as above applies.
    Event<TcpSendCompleteEventArgs> onSendComplete;

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

    // Queue data for a client and return at once, without waiting for it to be
    // written. A falsy result means nothing was queued and `error` says why;
    // otherwise `id` identifies this send in onSendComplete, which fires
    // exactly once for it.
    SendResult sendAsync(int clientId, const void* data, size_t size);
    SendResult sendAsync(int clientId, std::vector<char>&& data);   // takes the buffer, no copy
    SendResult sendAsync(int clientId, const std::string& message);

    // Queue data for every client. Returns how many accepted it; the payload is
    // buffered once and shared, not copied per client.
    int broadcastAsync(const void* data, size_t size);
    int broadcastAsync(const std::vector<char>& data);
    int broadcastAsync(const std::string& message);

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

    // Set the high-water mark for one client's send queue, in bytes
    // (0 = unlimited). Defaults to 16 MB.
    //
    // It is a mark, not a cap: sendAsync() is refused with QueueFull only when
    // the queue ALREADY holds this much, so a single message of any size still
    // goes through on an empty queue and there is no separate "too large" case.
    // A full queue does not disconnect anyone — only the idle timeout does.
    // send() waits for room instead of failing, so QueueFull is something only
    // sendAsync() can return.
    void setSendAsyncBufferSize(size_t bytes);
    size_t getSendAsyncBufferSize() const;

    // How much this client has queued and not yet completed, in bytes.
    // 0 for an unknown client.
    size_t getSendAsyncPendingBytes(int clientId) const;

    // -------------------------------------------------------------------------
    // Information retrieval
    // -------------------------------------------------------------------------

    // Listening port
    int getPort() const;

private:
    void acceptThreadFunc();
    void clientThreadFunc(int clientId);
    void writerThreadFunc(int clientId, std::shared_ptr<internal::TcpSendChannel> ch);
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
    std::unordered_map<int, std::thread> clientWriters_;

    // Shared: every send() looks a channel up through here, and so does every
    // getClientCount() a draw loop makes. Those are reads, and readers of a
    // plain mutex would take turns for no reason. Only registering, removing
    // and disconnecting a client write.
    mutable std::shared_mutex clientsMutex_;

    int nextClientId_ = 1;
    size_t receiveBufferSize_ = 65536;
    std::atomic<float> sendTimeout_{60.0f};   // idle, not total; read by every sending thread
    std::atomic<size_t> sendAsyncBufferSize_{16 * 1024 * 1024};   // per client high-water mark
    std::atomic<uint64_t> nextSendId_{0};     // 0 is reserved for "not queued"

    // Look up a client's send channel without holding clientsMutex_ during the send
    std::shared_ptr<internal::TcpSendChannel> findChannel(int clientId) const;

    // Every channel, taken under one lock. broadcast() used to take the lock
    // once for the id list and again for each id it then sent to.
    std::vector<std::pair<int, std::shared_ptr<internal::TcpSendChannel>>> snapshotChannels() const;

    // Put an item on a client's queue. `waiter` non-null means a synchronous
    // send: it waits for room rather than being refused with QueueFull.
    SendResult enqueue(const std::shared_ptr<internal::TcpSendChannel>& ch, int clientId,
                       internal::TcpSendItem&& item);

    // Report one finished item: release its share of the queue, wake a
    // synchronous sender, then fire onSendComplete
    void completeSend(int clientId, const std::shared_ptr<internal::TcpSendChannel>& ch,
                      const internal::TcpSendItem& item, SendError error, size_t bytesSent);

    // Refuse further sends, wake everything waiting on the channel, shut the
    // socket down and join the writer. The writer closes the descriptor itself,
    // on its way out.
    void closeChannel(int clientId, const std::shared_ptr<internal::TcpSendChannel>& ch);

    static std::atomic<int> instanceCount_;
    static void initWinsock();
    static void cleanupWinsock();
};

} // namespace trussc

namespace tc = trussc;
