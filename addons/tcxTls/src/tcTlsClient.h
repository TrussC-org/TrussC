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

namespace tcx {

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
    void tlsReceiveThreadFunc();

    std::thread tlsReceiveThread_;
};

}  // namespace tcx

// -----------------------------------------------------------------------------
// Backward compatibility: tcxTls historically lived in `trussc` (tc).
// The canonical namespace is now `tcx`; this alias keeps existing
// `tc::TlsClient` code compiling. DEPRECATED — will be removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxTls README for migration.)
// -----------------------------------------------------------------------------
namespace trussc {
using tcx::TlsClient;
}  // namespace trussc
