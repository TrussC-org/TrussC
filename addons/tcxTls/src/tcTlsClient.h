// =============================================================================
// tcTlsClient.h - TLS Client Socket
// Inherits from TcpClient and provides encrypted communication using mbedTLS
// =============================================================================
#pragma once

#include "tc/network/tcTcpClient.h"
#include <mutex>
#include <thread>
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
    // destroys the client) the run retains its State and transport until the
    // worker returns; see TcpClient's Events comment.
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
    friend class tcx::websocket::WebSocketClient;
    // Valid on the thread delivering onConnect, for the current attempt.
    std::chrono::steady_clock::time_point getTcpConnectTime() const;
    struct TlsContext;
    struct TlsTransport;
    std::shared_ptr<TlsTransport> transport_;

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
