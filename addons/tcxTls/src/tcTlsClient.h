// =============================================================================
// tcTlsClient.h - TLS Client Socket
// Inherits from TcpClient and provides encrypted communication using mbedTLS
// =============================================================================
#pragma once

#include "tc/network/tcTcpClient.h"
#include <atomic>
#include <chrono>
#include <string>

// Forward declarations (don't expose mbedtls headers)
struct mbedtls_ssl_context;
struct mbedtls_ssl_config;
struct mbedtls_x509_crt;
struct mbedtls_ctr_drbg_context;
struct mbedtls_entropy_context;

namespace tcx::websocket {
class WebSocketClient;
}

namespace tcx::tls {

using namespace tc;  // core types (TcpClient, ...)

// =============================================================================
// TlsClient Class (inherits from TcpClient)
// =============================================================================
class TlsClient : public tc::TcpClient {
public:
    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    TlsClient();

    // Disconnects without onDisconnect and returns once every thread of the
    // client has ended, one that a listener's connect() or disconnect() let
    // go of included. On the receive thread itself (an inline listener that
    // destroys the client) it detaches that thread instead, which then stops
    // without reading the client (#262); see TcpClient's Events comment.
    ~TlsClient() override;

    // Non-copyable
    TlsClient(const TlsClient&) = delete;
    TlsClient& operator=(const TlsClient&) = delete;

    // -------------------------------------------------------------------------
    // TLS Configuration
    // -------------------------------------------------------------------------

    // Set CA certificate (PEM format string)
    // Without an explicitly supplied CA, default trust anchors are loaded
    // lazily: Windows uses the union of the ROOT store and bundled Mozilla
    // CAs; macOS/Linux use an OS bundle file, falling back to the bundled CAs.
    // The Windows union also trusts bundled roots removed from the OS store;
    // it does not filter the Windows Disallowed store. A successful explicit
    // setCACertificate() / setCACertificateFile() skips default CA loading.
    bool setCACertificate(const std::string& pemData);

    // Load CA certificate from file
    bool setCACertificateFile(const std::string& path);

    // Disable server certificate verification (for testing, not recommended for production)
    void setVerifyNone();

    // Set hostname verification (default is connection host)
    void setHostname(const std::string& hostname);

    // Time allowed for the TLS handshake, in seconds, counted from the moment
    // the TCP connection is up. Default 15. 0 = no deadline. When it runs
    // out, the connection is closed and the client fires onError and then
    // onConnect(false) with the message "TLS handshake timeout". The TCP
    // connect before it uses setConnectTimeout() (default: the OS deadline).
    // Applies to the next connect().
    void setHandshakeTimeout(float seconds);

    // -------------------------------------------------------------------------
    // Connection Management (override TcpClient)
    // -------------------------------------------------------------------------

    // Connect to server (TCP connection + TLS handshake)
    bool connect(const std::string& host, int port) override;

    // Disconnect
    void disconnect() override;
    bool isConnecting() const override;

    // -------------------------------------------------------------------------
    // Data Send/Receive (override TcpClient)
    // -------------------------------------------------------------------------

    // Receive data (TLS encrypted)
    bool send(const void* data, size_t size) override;
    bool send(const std::vector<char>& data) override;
    bool send(const std::string& message) override;

    // Internal network processing (overrides TcpClient)
    void processNetwork() override;

    // -------------------------------------------------------------------------
    // TLS Information
    // -------------------------------------------------------------------------

    // Get current cipher suite name
    std::string getCipherSuite() const;

    // Get TLS version string
    std::string getTlsVersion() const;

private:
    // WebSocketClient counts its 101 deadline from getTcpConnectTime().
    friend class tcx::websocket::WebSocketClient;

    // When the TCP connection of the current attempt came up. Only valid on
    // the thread that fires onConnect (the handshake thread).
    std::chrono::steady_clock::time_point getTcpConnectTime() const { return handshakeStart_; }

    // mbedTLS context (PIMPL pattern)
    struct TlsContext;
    TlsContext* ctx_ = nullptr;

    std::string hostname_;
    bool verifyNone_ = false;
    std::atomic<bool> handshakePending_{false};
    bool handshakeStarted_ = false;
    // True once the user has called setCACertificate() or setCACertificateFile().
    // When true, ensureDefaultCAsLoaded() is skipped — the user's explicit set
    // wins and replaces any default. When false and !verifyNone_, default CAs
    // are lazily loaded on first handshake (OS store, then bundled fallback).
    bool caUserProvided_ = false;
    bool caAutoLoadAttempted_ = false;

    // Default for setHandshakeTimeout(), in seconds. Why 15:
    //  - The OS never times out a peer that accepted the TCP connection and
    //    then stays silent (a hung server, a captive portal, a middlebox), so
    //    without a deadline the handshake waits forever and no event fires.
    //  - A TLS handshake is a few round trips and normally finishes in well
    //    under a few seconds, even on slow links.
    //  - The value is deliberately generous: a too-short timeout broke
    //    Schannel renegotiation in tcxCurl (commit a3b79116). A longer value
    //    only delays noticing a stalled server.
    // The TCP connect stage keeps the OS deadline unless setConnectTimeout() is used.
    static constexpr float defaultHandshakeTimeout_ = 15.0f;

    // setHandshakeTimeout()'s value. Atomic: read by the receive thread.
    std::atomic<float> handshakeTimeout_{defaultHandshakeTimeout_};
    // When the TCP connection came up (the handshake deadline counts from
    // here). Written and read by the thread that runs the handshake.
    std::chrono::steady_clock::time_point handshakeStart_;

    // disconnect()'s work. notify: fire onDisconnect ("Disconnected by
    // client") if the client was connected. The destructor passes false.
    void disconnectImpl(bool notify, bool stopConnect = true);
    int writeSendStep(const void* data, size_t size, bool& forWrite, int& error) override;
    std::mutex tlsMutex_;

    // Clear the connection flags, close the socket and reset the SSL
    // context, leaving tlsReceiveThread_ alone. The receive thread calls it
    // on itself when a handshake fails: the thread stays owned (joinable),
    // so disconnect() and the destructor still join it while it notifies.
    // Only a listener that calls connect() or disconnect() there lets go of
    // it (tlsKeptThreads_ keeps it for a later join).
    void teardown();

    // Shut down and close the socket, if there is one
    void closeSocket();

    // Free and re-initialise the SSL context and config
    void resetSslContext();

    // One handshake step. Stopped: the handshake failed and was reported
    // (or a local disconnect() ended it); the calling thread must stop
    // without reading the client again, since a listener may have destroyed
    // it.
    enum class HandshakeStep { Done, InProgress, Stopped };
    HandshakeStep performHandshake(const AliveToken& alive);

    // Tear the handshake down and report it: onError(error), then
    // onConnect(false, connectMessage) unless a listener started a newer
    // attempt. Returns false (the calling thread stops); see performHandshake().
    bool failHandshake(const std::string& error, const std::string& connectMessage,
                       int code, const AliveToken& alive);

    // Lazy-load a trust anchor set on first handshake when the user hasn't
    // provided one explicitly. Tries the OS trust store first, then falls back
    // to the embedded Mozilla cacert.pem. Logs the source (logNotice) on
    // success and an error if no CAs could be loaded.
    void ensureDefaultCAsLoaded();

    // Receive thread (for TLS). alive: its own copy of alive_.
    void tlsReceiveThreadFunc(unsigned generation, AliveToken alive);

    // processNetwork()'s work, for the receive thread of that generation.
    // Returns false when the calling thread must stop without reading the
    // client again (see TcpClient::processNetworkStep()).
    bool processNetworkImpl(unsigned generation, const AliveToken& alive);

    std::thread tlsReceiveThread_;

    // tlsReceiveThread_ when a call on that very thread (a listener's
    // connect() or disconnect(), or the destructor) let go of it. Joined by
    // the next connect() or disconnect() on another thread, or by the
    // destructor (see tc/network/tcKeptThreads.h).
    tc::internal::KeptThreads tlsKeptThreads_;

    // Bumped by every connect(), before it sets any flag for the new
    // connection; its receive thread gets that value. A thread whose
    // generation is no longer current stops: processNetwork()'s receive loop and the loop
    // in tlsReceiveThreadFunc() both check it. So a listener on the receive
    // thread can reconnect without the old thread reading the new
    // connection: onReceive, onDisconnect, and onError or onConnect(false)
    // after a failed handshake (performHandshake() tears the failed
    // connection down before it notifies them).
    //
    // A listener on the receive thread may also destroy the client: the
    // thread checks its copy of alive_ after each notification, and after
    // onDisconnect and a failed handshake's onError / onConnect(false) it
    // stops without reading the client at all (#262).
    //
    // Teardown cancels and joins the connect worker before releasing TLS
    // state. Reentrant connection methods from listeners still require the
    // external caller to wait for that listener's call (see TcpClient Events).
    std::atomic<unsigned> tlsReceiveGeneration_{0};

    // Receive buffer, sized to receiveBufferSize_ by processNetwork()
    std::vector<unsigned char> tlsRecvBuf_;
};

}  // namespace tcx::tls

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::tls`. These
// silent aliases keep older code compiling: flat `tcx::TlsClient` and legacy
// `tc::TlsClient` / `trussc::TlsClient`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxTls README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using tls::TlsClient; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::tls::TlsClient; } // deprecated: remove at v1.0.0
