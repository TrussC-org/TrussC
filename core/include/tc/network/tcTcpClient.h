// =============================================================================
// tcTcpClient.h - TCP client socket
// =============================================================================
#pragma once
#include "tc/utils/tcAnnotations.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/network/tcKeptThreads.h"

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
public:
    // -------------------------------------------------------------------------
    // Events
    //
    // THREADING: with threading enabled (default), these events fire on the
    // internal receive/connect threads, not the main thread. A listener that
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
    // connect(), which can end in std::terminate (#261). Without threads
    // connectAsync() is connect(), and is overruled the same way.
    //
    // RECONNECTING ON THE RECEIVE THREAD: when an event fires on the receive
    // thread (onDisconnect for a remote close or an error, onReceive; for
    // TlsClient also onConnect and onError around the handshake), a plain
    // (inline) listener that calls connect() runs it on that old receive
    // thread, which cannot join itself. A listener that calls disconnect()
    // there is in the same position, and so is one that calls disconnect()
    // on the connect thread of connectAsync(). The client keeps such a
    // thread, which goes on with the rest of the listener and leaves its
    // loop, and joins it from another thread: the next connect() or
    // disconnect() waits for it, and so does the destructor.
    // connect(), connectAsync(), disconnect() and the destructor can wait
    // for a listener still running on one of the client's threads. Do not
    // call them while holding a lock that such a listener takes: the call
    // and the listener would wait for each other forever. Until #261 lands,
    // do not call disconnect() on the client from another thread until the
    // listener's call has returned, or the connection may complete after
    // disconnect() has returned.
    // Reconnecting from the main thread, as above, avoids all of this.
    //
    // DESTROYING FROM A LISTENER: an inline listener on the receive thread
    // may destroy the client (an owner that replaces it, as
    // WebSocketClient::connect() does). That thread cannot join itself: the
    // destructor detaches it and returns. Once a notification on the
    // receive thread returns, the thread checks whether the client still
    // exists before it reads the client again, and after onDisconnect (and
    // TlsClient's onConnect(false)) it stops without reading the client at
    // all (#262). The connect thread of connectAsync() is not covered: do
    // not destroy the client from a listener there (it returns into the
    // destroyed client, undefined behavior); destroy it from another thread,
    // or once the listener has returned (from a Deliver::Main listener, say).
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
    Event<TcpErrorEventArgs> onError;           // On error

    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    TcpClient();

    // Disconnects without onDisconnect and returns once every thread of the
    // client has ended, one that a listener's connect() or disconnect() let
    // go of included. On the receive thread itself (a listener that destroys
    // the client) it detaches that thread instead; on the connect thread it
    // must not run (see Events).
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

    // Set to false by the destructor. A receive thread holds its own copy:
    // after a notification it checks this copy, not the client, to find out
    // whether a listener destroyed the client (#262).
    using AliveToken = std::shared_ptr<std::atomic<bool>>;
    AliveToken alive_ = std::make_shared<std::atomic<bool>>(true);

    // processNetwork()'s work. alive: the caller's copy of alive_. Returns
    // false when the calling thread must stop at once without reading the
    // client again: it reported the end of the connection (onDisconnect, or
    // onConnect(false)), or a listener destroyed the client. The decision to
    // stop is made before the notification, since its listeners may destroy,
    // disconnect or reconnect the client.
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

    // Atomic: setReceiveBufferSize() may run on any thread (a listener on the
    // receive thread, say) while a receive thread reads it
    std::atomic<size_t> receiveBufferSize_{65536};
    std::mutex sendMutex_;

    // Atomic, both: a receive thread that a listener's disconnect() let go of
    // may still read them while the app calls setUseThread() or connect()
#ifdef __EMSCRIPTEN__
    std::atomic<bool> useThread_{false};
#else
    std::atomic<bool> useThread_{true};
#endif
    EventListener updateListener_;
    std::atomic<bool> connectPending_{false};

private:
    void receiveThreadFunc(unsigned generation, AliveToken alive);
    void connectThreadFunc(const std::string& host, int port);

    // Close the socket and release the receive thread (connectThread_ is left alone)
    void resetConnection();

    // disconnect()'s work. notify: fire onDisconnect ("Disconnected by
    // client") if the client was connected. The destructor passes false.
    void disconnectImpl(bool notify);

    std::thread receiveThread_;
    std::thread connectThread_;

    // receiveThread_ or connectThread_ when a call on that very thread (a
    // listener's connect() or disconnect(), or the destructor) let go of it.
    // Joined by the next connect() or disconnect() on another thread, or by
    // the destructor (see tcKeptThreads.h).
    internal::KeptThreads keptThreads_;

    // Bumped by every connect(), before it sets any flag for the new
    // connection; its receive thread gets that value. A thread whose
    // generation is no longer current stops: processNetwork()'s receive loop and the loop
    // in receiveThreadFunc() both check it. So a listener on the receive
    // thread (onReceive or onDisconnect) can reconnect without the old
    // thread reading the new connection's socket.
    //
    // Not covered: the reconnect itself. connect() on the receive thread
    // lets go of that thread (keptThreads_ keeps it) and then, on it,
    // creates the socket, resolves the host, connects (blocking), fires
    // onConnect and starts the new receive thread. disconnect() called on
    // the receive thread lets go of it the same way. The next connect() or
    // disconnect() on another thread, or the destructor, joins it, but
    // socket_ is not atomic: a disconnect() from another thread before the
    // listener's call has returned races it. Hence the rule in the Events
    // comment above. The fix belongs to #261 (a cancellable connect).
    // Destruction by a listener on the receive thread itself is covered:
    // see alive_.
    std::atomic<unsigned> receiveGeneration_{0};

    // Receive buffer, sized to receiveBufferSize_ by processNetwork()
    std::vector<char> recvBuf_;
};

} // namespace trussc

namespace tc = trussc;
