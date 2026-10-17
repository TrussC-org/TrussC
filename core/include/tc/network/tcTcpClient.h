// =============================================================================
// tcTcpClient.h - TCP client socket
// =============================================================================
#pragma once
#include "tc/utils/tcAnnotations.h"
#include "tc/network/tcNetworkState.h"
#include "tc/utils/tcKeptThreads.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/network/tcTcpSendChannel.h"
#include <chrono>
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
    #ifndef INVALID_SOCKET
    #define INVALID_SOCKET -1
    #endif
    #define SOCKET_ERROR -1
#endif
namespace trussc {
namespace internal { struct TcpClientTestAccess; }
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
class TC_PLATFORMS("macos,windows,linux,android,ios") TcpClient {
    friend struct internal::TcpClientTestAccess;
protected:
    struct State;
    // A transport belongs to the run, so asynchronous I/O never calls a handle.
    struct Transport {
        virtual ~Transport() = default;
        virtual bool connectSocket(State&, const std::string& host, int port);
        virtual int writeSendStep(State&, const void*, size_t, bool& forWrite, int& error);
        virtual bool isConnecting(const State&) const;
        virtual bool processNetworkStep(State&, const std::shared_ptr<std::atomic<bool>>& alive);
    };
    void setTransport(std::shared_ptr<Transport> transport);
    struct State : std::enable_shared_from_this<State> {
        ~State();
        internal::EventSnapshotDelivery<TcpConnectEventArgs> onConnect;
        internal::EventSnapshotDelivery<TcpReceiveEventArgs> onReceive;
        internal::EventSnapshotDelivery<TcpDisconnectEventArgs> onDisconnect;
        internal::EventSnapshotDelivery<TcpSendCompleteEventArgs> onSendComplete;
        internal::EventSnapshotDelivery<TcpErrorEventArgs> onError;
        bool connect(const std::string& host, int port);
        void connectAsync(const std::string& host, int port);
        void disconnect();
        bool isConnected() const;
        bool isConnecting() const;
        bool send(const void* data, size_t size);
        bool send(const std::vector<char>& data);
        bool send(const std::string& message);
        SendResult sendAsync(const void* data, size_t size);
        SendResult sendAsync(std::vector<char>&& data);
        SendResult sendAsync(const std::string& message);
        void setSendTimeout(float seconds);
        void setConnectTimeout(float seconds);
        // A stalled network peer can retain queued sends; a high-water mark limits further queue growth. Above the mark, sendAsync refuses with QueueFull and send waits; a single send can exceed the mark.
        void setSendAsyncBufferSize(size_t bytes);
        // A stalled network peer can retain queued sends; a high-water mark limits further queue growth. Above the mark, sendAsync refuses with QueueFull and send waits; a single send can exceed the mark.
        size_t getSendAsyncBufferSize() const;
        size_t getSendAsyncPendingBytes() const;
        // Network reads use finite chunks to bound each receive allocation. Overflow continues in subsequent reads.
        void setReceiveBufferSize(size_t size);
        void setBlocking(bool blocking);
        void setUseThread(bool useThread);
        bool isUsingThread() const;
        void processNetwork();
        std::string getRemoteHost() const;
        int getRemotePort() const;
        using AliveToken = std::shared_ptr<std::atomic<bool>>;
        void stopConnectThread();
        bool prepareConnect();
        bool connectSocket(const std::string& host, int port);
        int checkPendingConnect(int waitMs, int& error);
        void closeClientSocket();
        void waitClientSocket(bool forWrite, int ms);
        void startSendChannel();
        bool processSendQueue(const AliveToken& alive);
        void stopSendChannel();
        int writeSendStep(const void* data, size_t size, bool& forWrite, int& error);
        std::mutex socketMutex_;
        void notifyError(const std::string& msg, int code = 0);
        AliveToken alive_ = std::make_shared<std::atomic<bool>>(true);
        bool processNetworkStep(const AliveToken& alive);
    #ifdef _WIN32
        SOCKET socket_ = INVALID_SOCKET;
    #else
        int socket_ = -1;
    #endif
        std::string remoteHost_;
        int remotePort_ = 0;
        std::atomic<bool> running_{false};
        std::atomic<bool> connected_{false};
        // Bound (security): network reads use finite chunks to bound each receive allocation. Overflow continues in later reads.
        std::atomic<size_t> receiveBufferSize_{65536};
    #ifdef __EMSCRIPTEN__
        std::atomic<bool> useThread_{false};
    #else
        std::atomic<bool> useThread_{true};
    #endif
        EventListener updateListener_;
        std::atomic<bool> connectPending_{false};
        void receiveThreadFunc(unsigned generation, AliveToken alive);
        void connectThreadFunc(const std::string& host, int port, unsigned attempt);
        void resetConnection();
        void disconnectImpl(bool notify, bool stopConnect = true);
        std::thread receiveThread_;
        std::thread connectThread_;
        struct ClientSendChannel;
        bool drainSendChannel(const std::shared_ptr<ClientSendChannel>& ch, bool wait, const AliveToken& alive);
        std::thread writerThread_;
        mutable std::mutex channelMutex_;
        std::shared_ptr<ClientSendChannel> sendChannel_;
        std::atomic<float> sendTimeout_{60.0f};
        // Bound (security): a stalled network peer can retain queued sends; a high-water mark limits further queue growth. Overflow refuses sendAsync with QueueFull or makes send wait; a single send can exceed the mark.
        std::atomic<size_t> sendAsyncBufferSize_{16 * 1024 * 1024};
        std::atomic<uint64_t> nextSendId_{0};
        std::atomic<float> connectTimeout_{0.0f};
        std::chrono::steady_clock::time_point connectStart_;
        std::atomic<bool> connectCancelled_{false};
        std::atomic<bool> asyncConnecting_{false};
        std::atomic<unsigned> connectAttempt_{0};
        mutable std::mutex targetMutex_;
        std::string asyncHost_;
        int asyncPort_ = 0;
        std::shared_ptr<ClientSendChannel> sendChannel() const;
        SendResult enqueue(internal::TcpSendItem&& item);
        void writerThreadFunc(std::shared_ptr<ClientSendChannel> channel, AliveToken alive);
        internal::KeptThreads keptThreads_;
        internal::KeptThreads keptWriters_;
        std::atomic<unsigned> receiveGeneration_{0};
        std::vector<char> recvBuf_;
        std::shared_ptr<Transport> transport_ = std::make_shared<Transport>();
        bool processPlainNetworkStep(const AliveToken& alive);
    };
private:
    internal::AtomicSharedPtr<State> state_{std::make_shared<State>()};
public:
    // -------------------------------------------------------------------------
    // Events
    //
    // THREADING: with threading enabled (default), these events fire on the
    // internal receive/connect/send threads, not the main thread. A listener that
    // touches the Node tree, GPU resources, or unguarded app state must opt
    // into main-thread delivery:
    //
    //   listener = client.onReceive.listen(fn, Deliver::Main);
    //
    // Deliver::Main copies the payload and runs the listener at the start of
    // the next frame (see tcEvent.h). Plain listen(fn) runs inline on the
    // firing thread. With setUseThread(false) everything runs on the main
    // thread and this does not apply.
    //
    // onDisconnect: TcpDisconnectEventArgs::reason says why the connection
    // ended.
    //  - "Connection closed by remote" (wasClean true): the peer closed it.
    //    Fires on the receive thread.
    //  - "Connection error" (wasClean false; TlsClient: "TLS error: ..."):
    //    an error ended it. Fires on the receive thread.
    //  - "Send failed" (wasClean false): a send failed or timed out.
    //    Fires on the writer thread, or inline without threads.
    //  - "Disconnected by client" (wasClean true): the app ended it, with
    //    disconnect() or with connect() on a connected client. Fires
    //    synchronously on the calling thread, before that call returns.
    // Without threads (setUseThread(false), the default on the web) the first
    // two fire on the main thread instead, from the update event.
    // The destructor disconnects without firing it. An auto-reconnect
    // listener should not reconnect on "Disconnected by client", the app's
    // own doing:
    //
    //   listener = client.onDisconnect.listen([&](TcpDisconnectEventArgs& e) {
    //       if (e.reason == "Disconnected by client") return;  // the app did it
    //       // e.wasClean: true if the peer closed it, false if an error did
    //       reconnectPending = true;  // reconnect from update(), main thread
    //   });
    //
    // A listener that reconnects with connect() from inside connect()'s own
    // disconnect anyway is overruled: connect() closes that connection again,
    // without another notification, and connects where it was asked to.
    // With threads, do not call connectAsync() from such a listener: the
    // connect thread it starts runs connect() at the same time as the outer
    // connect(), which is unsupported. Without threads
    // connectAsync() is connect(), and is overruled the same way.
    //
    // Inline TCP listeners may disconnect, reconnect with connectAsync(), or
    // destroy the client. Workers retain their own State and never read the
    // public handle after a notification. Teardown joins from another thread;
    // on its own worker it returns without self-join. A restart gets a fresh
    // State.
    // Do not hold a listener's lock while joining it from another thread.
    // Simultaneous reentrant connection management remains unsupported.
    //
    // onConnect(false): a failed attempt reports it from connectAsync(), from
    // a pending connect without threads, and from a failed TLS handshake (a
    // failed blocking connect() returns false instead). An attempt that a
    // newer attempt has replaced does not report it: when an onError
    // listener reconnects, the failed attempt reports nothing more once the
    // client is connected or connecting again, and only the newer attempt
    // reports its result. connect() still returns false for the failed
    // attempt while isConnected() is true for the newer one.
    // -------------------------------------------------------------------------
    Event<TcpConnectEventArgs> onConnect;       // On connection complete
    Event<TcpReceiveEventArgs> onReceive;       // On data receive
    Event<TcpDisconnectEventArgs> onDisconnect; // On disconnect
    Event<TcpSendCompleteEventArgs> onSendComplete; // Writer thread, or processNetwork() without threads
    Event<TcpErrorEventArgs> onError;           // On error
    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    TcpClient();
    // Disconnects without onDisconnect. On a TCP worker, retains that worker's
    // State until it returns; from another thread, waits for completion.
    virtual ~TcpClient();
    // Copy prohibited
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    // Only stopped sockets move their State. A running socket is stopped first
    // with one Error "moved while running; stopped". Listeners stay with the
    // old handle; the moved-from handle is ready for a new connection.
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
    // True during an asynchronous TCP connect (TLS also includes its handshake).
    virtual bool isConnecting() const;
    // -------------------------------------------------------------------------
    // Data send/receive (virtual - can be overridden in TlsClient)
    // -------------------------------------------------------------------------
    // Send data and wait until the payload is written or an error occurs,
    // including setSendTimeout() seconds without progress (60 s by default).
    // On the main thread (update/draw, input handlers, Deliver::Main listeners),
    // use sendAsync() to avoid waiting.
    virtual bool send(const void* data, size_t size);
    virtual bool send(const std::vector<char>& data);
    virtual bool send(const std::string& message);
    // Queue owned bytes; completion reports through onSendComplete.
    // An immediate rejection (not connected, queue full, connection closed)
    // also fires onError right away on the calling thread: sending again from
    // that listener recurses.
    SendResult sendAsync(const void* data, size_t size);
    SendResult sendAsync(std::vector<char>&& data);
    SendResult sendAsync(const std::string& message);
    // Idle send deadline, default 60 seconds; 0 waits indefinitely.
    void setSendTimeout(float seconds);
    // TCP connect deadline, default 0 (the OS deadline). Does not cover DNS or TLS.
    void setConnectTimeout(float seconds);
    // Same high-water mark as TcpServer: 16 MB; 0 is unlimited. A single
    // payload may exceed the mark. send() waits for room; sendAsync() refuses it.
    // A stalled network peer can retain queued sends; a high-water mark limits further queue growth. Above the mark, sendAsync refuses with QueueFull and send waits; a single send can exceed the mark.
    void setSendAsyncBufferSize(size_t bytes);
    // A stalled network peer can retain queued sends; a high-water mark limits further queue growth. Above the mark, sendAsync refuses with QueueFull and send waits; a single send can exceed the mark.
    size_t getSendAsyncBufferSize() const;
    size_t getSendAsyncPendingBytes() const;
    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------
    // Set receive buffer size
    // Network reads use finite chunks to bound each receive allocation. Overflow continues in subsequent reads.
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
private:
    std::shared_ptr<internal::NetworkDelivery> delivery_ = std::make_shared<internal::NetworkDelivery>();
    std::shared_ptr<internal::EventSnapshotDelivery<TcpConnectEventArgs>::Target> onConnectTarget_;
    std::shared_ptr<internal::EventSnapshotDelivery<TcpReceiveEventArgs>::Target> onReceiveTarget_;
    std::shared_ptr<internal::EventSnapshotDelivery<TcpDisconnectEventArgs>::Target> onDisconnectTarget_;
    std::shared_ptr<internal::EventSnapshotDelivery<TcpSendCompleteEventArgs>::Target> onSendCompleteTarget_;
    std::shared_ptr<internal::EventSnapshotDelivery<TcpErrorEventArgs>::Target> onErrorTarget_;
    void attachTargets();
    void freshState();
    void joinRetired();
    std::vector<std::shared_ptr<State>> retired_;
protected:
    using AliveToken = std::shared_ptr<std::atomic<bool>>;
    void notifyError(const std::string& msg, int code = 0);
    // Cancel and join before derived connection state is released. Self-join
    // is kept for a later caller, as with the receive thread.
    void stopConnectThread();
    bool prepareConnect();
    bool connectSocket(const std::string& host, int port);
    // 1 connected, 0 pending, -1 failed, -2 cancelled. waitMs <= 100.
    int checkPendingConnect(int waitMs, int& error);
    void closeClientSocket();
    void waitClientSocket(bool forWrite, int ms);
    void startSendChannel();
    bool processSendQueue(const AliveToken& alive);
    bool processNetworkStep(const AliveToken& alive);
    void stopSendChannel();
    // Non-blocking plain-socket write step; -1 means retry, -2 fatal.
    int writeSendStep(const void* data, size_t size, bool& forWrite, int& error);
    std::shared_ptr<State> state() const { return internal::sharedLoad(state_); }
    AliveToken alive_ = std::make_shared<std::atomic<bool>>(true);
    std::string remoteHost_;
    int remotePort_ = 0;
    EventListener updateListener_;
};
} // namespace trussc
namespace tc = trussc;
