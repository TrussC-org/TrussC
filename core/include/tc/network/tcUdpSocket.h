#pragma once
#include "tc/utils/tcAnnotations.h"
#include "tc/network/tcNetworkState.h"
#include "tc/utils/tcKeptThreads.h"
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
#include "../utils/tcOnceGate.h"
#include "tc/utils/tcKeptThreads.h"
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
namespace internal { struct UdpSocketBindAddressTestAccess; struct UdpSocketTestAccess; struct UdpSocketLifecycleTestAccess; struct ExternalUdpDeliveryTestAccess; }
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
class TC_PLATFORMS("macos,windows,linux,android,ios") UdpSocket {
    // The handle and every receive run share the same OS socket until close().
    struct Socket {
        ~Socket();
        std::atomic<SocketHandle> handle{INVALID_SOCKET_HANDLE};
        std::atomic<bool> closed{false};   // shut down by close(); never reused
        std::mutex mutex;
    };
    struct BindConfiguration {
        std::mutex mutex;
        std::string address;
        std::string error;
    };
    struct State : std::enable_shared_from_this<State> {
        explicit State(std::shared_ptr<Socket> socket = std::make_shared<Socket>(),
                       std::shared_ptr<BindConfiguration> bindConfiguration = std::make_shared<BindConfiguration>())
            : socket_(std::move(socket)), bindConfiguration_(std::move(bindConfiguration)) {}
        internal::EventSnapshotDelivery<UdpReceiveEventArgs> onReceive;
        internal::EventSnapshotDelivery<UdpErrorEventArgs> onError;
        bool create();
        void setBindAddress(const std::string& address);
        bool bind(int port, bool startReceiving = true);
        bool connect(const std::string& host, int port);
        void close();
        bool sendTo(const std::string& host, int port, const void* data, size_t size);
        bool sendTo(const std::string& host, int port, const std::string& message);
        bool send(const void* data, size_t size);
        bool send(const std::string& message);
        int receive(void* buffer, size_t bufferSize);
        int receive(void* buffer, size_t bufferSize, std::string& remoteHost, int& remotePort);
        bool startReceiving();
        void stopReceiving();
        bool isReceiving() const { return receiving_.load(); }
        bool setNonBlocking(bool nonBlocking);
        bool setBroadcast(bool enable);
        bool setReuseAddress(bool enable);
        bool setReusePort(bool enable);
        bool joinMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr = "");
        bool leaveMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr = "");
        bool setMulticastTTL(int ttl);
        bool setMulticastLoopback(bool enable);
        bool setMulticastInterface(const std::string& interfaceAddr);
        // Untrusted datagram floods can fill the OS queue; the configured buffer keeps queued memory finite. On overflow, the OS drops datagrams.
        bool setReceiveBufferSize(int size);
        bool setSendBufferSize(int size);
        bool setReceiveTimeout(int timeoutMs);
        void setUseThread(bool useThread);
        int getLocalPort() const { return localPort_; }
        void processNetwork();
        bool isValid() const { return socket_->handle != INVALID_SOCKET_HANDLE; }
        const std::string& getConnectedHost() const { return connectedHost_; }
        int getConnectedPort() const { return connectedPort_; }
        enum class ErrorKind { Resolve, Send, Receive };
        struct ErrorLogState {
            OnceGate gate{5.0};
            std::atomic<uint64_t> suppressed{0};
            std::atomic<bool> failedSinceSuccess{false};
        };
        void receiveThreadFunc();
        bool ensureSocket();
        void notifyError(const std::string& message, int code = 0);
        void notifyError(ErrorKind kind, const std::string& message, int code = 0);
        void notifyRecovery(ErrorKind kind);
        ErrorLogState errorLogs_[3];
        std::mutex errorLogMutex_;
        std::shared_ptr<Socket> socket_;
        // Shared across receive runs so a setter on a retired State remains effective.
        std::shared_ptr<BindConfiguration> bindConfiguration_;
        int localPort_ = 0;
        std::atomic<bool> bound_{false};
        bool reuseAddress_ = false;
        bool reusePort_ = false;
        std::string connectedHost_;
        int connectedPort_ = 0;
        std::thread receiveThread_;
        internal::KeptThreads keptThreads_;
        std::atomic<bool> receiving_{false};
        std::atomic<bool> shouldStop_{false};
    #ifdef __EMSCRIPTEN__
        bool useThread_ = false;
    #else
        bool useThread_ = true;
    #endif
        EventListener updateListener_;
        // Bound (platform): a UDP datagram fits within the IP length limit; a fixed receive buffer avoids per-packet growth. Overflow truncates an oversized datagram on POSIX or fails the receive on Windows.
        static constexpr size_t RECEIVE_BUFFER_SIZE = 65536;
        std::vector<char> receiveBuffer_; // allocated only before publishing a receive run
    };
    std::shared_ptr<Socket> socketOwner_ = std::make_shared<Socket>();
    internal::AtomicSharedPtr<State> state_{std::make_shared<State>(socketOwner_)};
public:
    // Events
    //
    // THREADING: when the receive thread is active (bind() default), onReceive
    // fires ON THE RECEIVE THREAD, not the main thread. onError fires on
    // whichever thread the failing call ran on (receive thread for receive
    // errors, the caller's thread for send/bind/connect errors). A listener
    // that touches the Node tree, GPU resources, or unguarded app state must
    // opt into main-thread delivery:
    //
    //   listener = socket.onReceive.listen(fn, Deliver::Main);
    //
    // Deliver::Main copies the payload and runs the listener at the start of
    // the next frame (see tcEvent.h). Plain listen(fn) runs inline on the
    // receive thread — fastest, but you handle the synchronization.
    // With setUseThread(false) everything runs on the main thread and this
    // does not apply.
    //
    // Inline listeners may stop, restart, close or destroy this socket.
    // Each receive run owns its State until its worker returns. Teardown joins
    // from another thread; from its own worker it returns without self-join.
    // Do not hold a listener's lock while joining it from another thread.
    Event<UdpReceiveEventArgs> onReceive;   // On data receive
    Event<UdpErrorEventArgs> onError;       // On error
    UdpSocket();
    // Closes the socket; on the receive thread the worker retains its State.
    ~UdpSocket();
    // Copy prohibited
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    // Only stopped sockets move their State. A running socket is stopped first
    // with one Error "moved while running; stopped". Listeners stay with the
    // old handle; the moved-from handle is ready for a new run.
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------
    // Create socket (usually not needed as bind/connect auto-create)
    bool create();
    // Choose a numeric IPv4 listen address. Empty or "0.0.0.0" selects every
    // interface (default); "127.0.0.1" accepts traffic from this machine only.
    // Invalid input, IPv6 and nonlocal addresses make bind() return false
    // and report onError. Configure before bind().
    // A change while bound (including stopped reception) logs a Warning,
    // leaves the active socket unchanged, and applies after close() and bind().
    void setBindAddress(const std::string& address);
    // Bind port for receiving
    // startReceiving=true automatically starts receive thread
    // Without setReuseAddress(true) / setReusePort(true), binding is exclusive
    // against binds to the same address (SO_EXCLUSIVEADDRUSE on Windows).
    // With a specific setBindAddress(), macOS and Windows still allow a socket
    // binding all addresses with reuse enabled to share the port; Linux refuses.
    bool bind(int port, bool startReceiving = true);
    // Set destination (after setting, can send via send(data, size))
    bool connect(const std::string& host, int port);
    // Close socket. Returns once the receive thread has ended (see
    // onReceive for a call on that thread itself).
    // startReceiving() (bind() calls it), stopReceiving(), close() and the
    // destructor can wait for a listener still running on the receive
    // thread. Do not call them while holding a lock that such a listener
    // takes: the call and the listener would wait for each other forever.
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
    // Stop receiving only. The socket, bound port, options, multicast
    // memberships and send() remain available across stop/start.
    // Returns once it has ended (see onReceive for a
    // call on that thread itself).
    // startReceiving() (bind() calls it), stopReceiving(), close() and the
    // destructor can wait for a listener still running on the receive
    // thread. Do not call them while holding a lock that such a listener
    // takes: the call and the listener would wait for each other forever.
    void stopReceiving();
    // Whether receiving
    bool isReceiving() const { return internal::sharedLoad(state_)->receiving_.load(); }
    // -------------------------------------------------------------------------
    // Settings/Information
    // -------------------------------------------------------------------------
    // Set non-blocking mode
    bool setNonBlocking(bool nonBlocking);
    // Allow broadcast
    bool setBroadcast(bool enable);
    // Allow reuse (set before bind)
    bool setReuseAddress(bool enable);
    // Allow several sockets to bind the SAME port (set before bind). Needed when
    // multiple listeners share one multicast port — e.g. sACN/E1.31 on UDP 5568,
    // where another app (a console, QLC+) may also be bound. Maps to SO_REUSEPORT
    // on POSIX; on Windows SO_REUSEADDR already grants this, so it maps to that.
    bool setReusePort(bool enable);
    // -------------------------------------------------------------------------
    // Multicast (IPv4)
    // -------------------------------------------------------------------------
    // Multicast addresses live in 224.0.0.0/4 (e.g. sACN/E1.31 uses
    // 239.255.x.x). SENDING to a group needs no membership — just sendTo() the
    // group address (optionally pick the outgoing NIC with
    // setMulticastInterface() / cap the hop count with setMulticastTTL()).
    // RECEIVING multicast requires joining the group AFTER bind() so the OS
    // delivers that group's datagrams to this socket.
    // Join a multicast group so this socket receives its datagrams. Call after
    // bind(). interfaceAddr selects which NIC to receive on (""/"0.0.0.0" =
    // default route). Joining the same group on several interfaces is allowed.
    bool joinMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr = "");
    // Stop receiving a previously joined group (sockets auto-leave on close()).
    bool leaveMulticastGroup(const std::string& groupAddr, const std::string& interfaceAddr = "");
    // Hop limit for OUTGOING multicast. Default 1 = stay on the local subnet
    // (the usual lighting-network case). Raise to cross routers.
    bool setMulticastTTL(int ttl);
    // Whether OUTGOING multicast loops back to local listeners on this host.
    // Default on (lets a sender + receiver in the same process see each other).
    bool setMulticastLoopback(bool enable);
    // Pick the NIC used for OUTGOING multicast (""/"0.0.0.0" = default route).
    bool setMulticastInterface(const std::string& interfaceAddr);
    // Set receive buffer size
    // Untrusted datagram floods can fill the OS queue; the configured buffer keeps queued memory finite. On overflow, the OS drops datagrams.
    bool setReceiveBufferSize(int size);
    // Set send buffer size
    bool setSendBufferSize(int size);
    // Set receive timeout (milliseconds, 0=infinite)
    bool setReceiveTimeout(int timeoutMs);
    // Set whether to use thread for receiving (Wasm must be false)
    void setUseThread(bool useThread);
    // Get bound port
    int getLocalPort() const { return internal::sharedLoad(state_)->localPort_; }
    // Internal update method (called by event listener if not using threads)
    void processNetwork();
    // Whether socket is valid
    bool isValid() const { return internal::sharedLoad(state_)->socket_->handle != INVALID_SOCKET_HANDLE; }
    // Destination information
    const std::string& getConnectedHost() const { return internal::sharedLoad(state_)->connectedHost_; }
    int getConnectedPort() const { return internal::sharedLoad(state_)->connectedPort_; }
private:
    std::shared_ptr<internal::NetworkDelivery> delivery_ = std::make_shared<internal::NetworkDelivery>();
    std::shared_ptr<internal::EventSnapshotDelivery<UdpReceiveEventArgs>::Target> onReceiveTarget_;
    std::shared_ptr<internal::EventSnapshotDelivery<UdpErrorEventArgs>::Target> onErrorTarget_;
    void attachTargets();
    friend struct internal::UdpSocketTestAccess;
    friend struct internal::UdpSocketBindAddressTestAccess;
    friend struct internal::ExternalUdpDeliveryTestAccess;
    friend struct internal::UdpSocketLifecycleTestAccess;
    using ErrorKind = State::ErrorKind;
    void notifyError(const std::string& message, int code = 0);
    void notifyError(ErrorKind kind, const std::string& message, int code = 0);
    void notifyRecovery(ErrorKind kind);
    void freshState();
    void joinRetired();
    std::vector<std::shared_ptr<State>> retired_;
};
} // namespace trussc
namespace tc = trussc;
