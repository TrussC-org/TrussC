// =============================================================================
// tcTlsClient.h - TLS Client Socket
// Inherits from TcpClient and provides encrypted communication using mbedTLS
// =============================================================================
#pragma once

#include "tc/network/tcTcpClient.h"
#include <string>

// Forward declarations (don't expose mbedtls headers)
struct mbedtls_ssl_context;
struct mbedtls_ssl_config;
struct mbedtls_x509_crt;
struct mbedtls_ctr_drbg_context;
struct mbedtls_entropy_context;

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
    ~TlsClient() override;

    // Non-copyable
    TlsClient(const TlsClient&) = delete;
    TlsClient& operator=(const TlsClient&) = delete;

    // -------------------------------------------------------------------------
    // TLS Configuration
    // -------------------------------------------------------------------------

    // Set CA certificate (PEM format string)
    bool setCACertificate(const std::string& pemData);

    // Load CA certificate from file
    bool setCACertificateFile(const std::string& path);

    // Disable server certificate verification (for testing, not recommended for production)
    void setVerifyNone();

    // Set hostname verification (default is connection host)
    void setHostname(const std::string& hostname);

    // -------------------------------------------------------------------------
    // Connection Management (override TcpClient)
    // -------------------------------------------------------------------------

    // Connect to server (TCP connection + TLS handshake)
    bool connect(const std::string& host, int port) override;

    // Disconnect
    void disconnect() override;

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
    // mbedTLS context (PIMPL pattern)
    struct TlsContext;
    TlsContext* ctx_ = nullptr;

    std::string hostname_;
    bool verifyNone_ = false;
    bool handshakePending_ = false;
    bool handshakeStarted_ = false;
    // True once the user has called setCACertificate() or setCACertificateFile().
    // When true, ensureDefaultCAsLoaded() is skipped — the user's explicit set
    // wins and replaces any default. When false and !verifyNone_, default CAs
    // are lazily loaded on first handshake (OS store, then bundled fallback).
    bool caUserProvided_ = false;
    bool caAutoLoadAttempted_ = false;

    // disconnect()'s work. notify: fire onDisconnect ("Disconnected by
    // client") if the client was connected. The destructor passes false.
    void disconnectImpl(bool notify);

    // Clear the connection flags, close the socket and reset the SSL
    // context, leaving tlsReceiveThread_ alone. The receive thread calls it
    // on itself when a handshake fails: the thread stays owned (joinable),
    // so disconnect() and the destructor still join it while it notifies.
    // Only a listener that calls connect() or disconnect() there detaches it.
    void teardown();

    // Shut down and close the socket, if there is one
    void closeSocket();

    // Free and re-initialise the SSL context and config
    void resetSslContext();

    // Perform TLS handshake
    bool performHandshake();

    // Lazy-load a trust anchor set on first handshake when the user hasn't
    // provided one explicitly. Tries the OS trust store first, then falls back
    // to the embedded Mozilla cacert.pem. Logs the source (logNotice) on
    // success and an error if no CAs could be loaded.
    void ensureDefaultCAsLoaded();

    // Receive thread (for TLS)
    void tlsReceiveThreadFunc(unsigned generation);

    // processNetwork()'s work, for the receive thread of that generation
    void processNetworkImpl(unsigned generation);

    std::thread tlsReceiveThread_;

    // Bumped by every connect(), before it sets any flag for the new
    // connection; its receive thread gets that value. A thread whose
    // generation is no longer current stops: processNetwork()'s receive loop and the loop
    // in tlsReceiveThreadFunc() both check it. So a listener on the receive
    // thread can reconnect without the old thread reading the new
    // connection: onReceive, onDisconnect, and onError or onConnect(false)
    // after a failed handshake (performHandshake() tears the failed
    // connection down before it notifies them).
    //
    // Not covered: the reconnect itself. connect() on the receive thread
    // detaches that thread and then, on it, creates the socket, resolves the
    // host, connects (blocking) and starts the new receive thread. A
    // disconnect() called on the receive thread detaches it the same way. No
    // one owns the detached thread meanwhile: disconnect() and the destructor
    // do not wait for it, and socket_ is not atomic. So, as for TcpClient
    // (see its Events comment): until a connect() or disconnect() called from
    // a listener on the receive thread has returned, do not destroy the
    // client, and do not call disconnect() on it from another thread. The fix
    // belongs to #261 (a cancellable connect) and #262.
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
