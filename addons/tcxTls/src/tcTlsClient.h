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

    // Perform TLS handshake
    bool performHandshake();

    // Lazy-load a trust anchor set on first handshake when the user hasn't
    // provided one explicitly. Tries the OS trust store first, then falls back
    // to the embedded Mozilla cacert.pem. Logs the source (logNotice) on
    // success and an error if no CAs could be loaded.
    void ensureDefaultCAsLoaded();

    // Receive thread (for TLS)
    void tlsReceiveThreadFunc(unsigned generation);

    std::thread tlsReceiveThread_;

    // Bumped for every receive thread started. A thread whose generation is
    // no longer current stops: processNetwork()'s receive loop and the loop
    // in tlsReceiveThreadFunc() both check it. So a listener on the receive
    // thread can reconnect without the old thread reading the new
    // connection: onReceive, onDisconnect, and onError or onConnect(false)
    // after a failed handshake (performHandshake() tears the failed
    // connection down before it notifies them).
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
