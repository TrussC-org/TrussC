// =============================================================================
// tcTlsClient.cpp - TLS Client Socket Implementation
// =============================================================================

#include "tcTlsClient.h"
#include <mutex>
#include "tcTlsCaInternal.h"
#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"
#include "tc/events/tcCoreEvents.h"

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>

#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <wincrypt.h>
#else
    #include <sys/stat.h>
#endif

using namespace tc;

namespace tcx::tls {

using tls_internal::countCerts;

// Upper bound on CA PEM file size. Real-world OS trust stores are well
// under 1 MB (typically ~250 KB; the embedded Mozilla bundle is ~360 KB).
// 16 MB is generous and guards against runaway reads if the path is
// unexpectedly large — e.g. a sysadmin symlinked /etc/ssl/cert.pem to
// the wrong target, or someone passed a non-PEM file to
// setCACertificateFile() by mistake.
inline constexpr std::streamsize kMaxCaPemBytes = 16 * 1024 * 1024;

// =============================================================================
// TLS Context Structure (PIMPL)
// =============================================================================
struct TlsClient::TlsContext {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    TlsContext() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&cacert);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);
    }

    ~TlsContext() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&cacert);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

// The TCP run retains this object. No TLS operation resolves a handle's State.
struct TlsClient::TlsTransport : TcpClient::Transport {
    TlsTransport();
    std::unique_ptr<TlsContext> ctx_ = std::make_unique<TlsContext>();
    std::string hostname_;
    bool verifyNone_ = false;
    bool caUserProvided_ = false;
    bool caAutoLoadAttempted_ = false;
    std::atomic<bool> handshakePending_{false};
    bool handshakeStarted_ = false;
    std::atomic<float> handshakeTimeout_{15.0f};
    std::chrono::steady_clock::time_point handshakeStart_;
    std::mutex tlsMutex_;
    std::vector<unsigned char> tlsRecvBuf_;
    std::atomic<unsigned> tlsReceiveGeneration_{0};
    bool setCACertificate(const std::string&);
    bool setCACertificateFile(const std::string&);
    void ensureDefaultCAsLoaded();
    void resetSslContext();
    void teardown(State&);
    enum class HandshakeStep { Done, InProgress, Stopped };
    bool setupHandshake(State&, const AliveToken&);
    HandshakeStep performHandshake(State&, const AliveToken&);
    bool failHandshake(State&, const std::string&, const std::string&, int, const AliveToken&);
    bool connectSocket(State&, const std::string&, int) override;
    int writeSendStep(State&, const void*, size_t, bool&, int&) override;
    bool isConnecting(const State& st) const override {
        return st.connectPending_ || (st.running_ && handshakePending_);
    }
    bool processNetworkStep(State&, const AliveToken&) override;
    std::string getCipherSuite(const State&) const;
    std::string getTlsVersion(const State&) const;
};

// =============================================================================
// mbedTLS Callbacks (socket send/receive)
// =============================================================================
static int mbedtls_net_send_callback(void* ctx, const unsigned char* buf, size_t len) {
#ifdef _WIN32
    SOCKET fd = *static_cast<SOCKET*>(ctx);
    int ret = ::send(fd, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
    if (ret < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
#else
    int fd = *static_cast<int*>(ctx);
    // TC_SEND_FLAGS: a peer that closed first makes this fail instead of
    // raising SIGPIPE
    int ret = static_cast<int>(::send(fd, buf, len, TC_SEND_FLAGS));
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
#endif
    return ret;
}

static int mbedtls_net_recv_callback(void* ctx, unsigned char* buf, size_t len) {
#ifdef _WIN32
    SOCKET fd = *static_cast<SOCKET*>(ctx);
    int ret = ::recv(fd, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    if (ret < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
#else
    int fd = *static_cast<int*>(ctx);
    int ret = static_cast<int>(::recv(fd, buf, len, 0));
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
#endif
    if (ret == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return ret;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================
TlsClient::TlsTransport::TlsTransport() {

    // Initialize random number generator
    const char* pers = "trussc_tls_client";
    int ret = mbedtls_ctr_drbg_seed(&ctx_->ctr_drbg, mbedtls_entropy_func,
                                     &ctx_->entropy,
                                     reinterpret_cast<const unsigned char*>(pers),
                                     strlen(pers));
    if (ret != 0) {
        logError() << "TlsClient: Failed to seed random generator: " << ret;
    }
}

// =============================================================================
// TLS Configuration
// =============================================================================
bool TlsClient::TlsTransport::setCACertificate(const std::string& pemData) {
    int ret = mbedtls_x509_crt_parse(&ctx_->cacert,
                                      reinterpret_cast<const unsigned char*>(pemData.c_str()),
                                      pemData.size() + 1);
    if (ret < 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        logError() << "TlsClient: Failed to parse CA certificate: " << errBuf;
        return false;
    }
    // Explicit user intent: skip the default-CA auto-load entirely.
    caUserProvided_ = true;
    return true;
}

bool TlsClient::TlsTransport::setCACertificateFile(const std::string& path) {
    // Open at end (ate) to get size via tellg without a separate stat()
    // call — works portably on POSIX and Windows.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        logError() << "TlsClient: Failed to open CA certificate file: " << path;
        return false;
    }
    std::streamsize size = file.tellg();
    if (size < 0) {
        logError() << "TlsClient: Failed to size CA certificate file: " << path;
        return false;
    }
    if (size > kMaxCaPemBytes) {
        logError() << "TlsClient: CA certificate file too large ("
                     << size << " bytes, limit " << kMaxCaPemBytes
                     << "): " << path;
        return false;
    }
    file.seekg(0);

    std::stringstream buffer;
    buffer << file.rdbuf();
    return setCACertificate(buffer.str());
}

// =============================================================================
// Default CA Loading (lazy)
// =============================================================================
void TlsClient::TlsTransport::ensureDefaultCAsLoaded() {
    caAutoLoadAttempted_ = true;

#ifdef _WIN32
    // Windows: enumerate the system ROOT store and feed each cert to mbedtls
    // as DER. Requires linking crypt32 (set in CMakeLists).
    std::vector<std::vector<unsigned char>> osCertificates;
    HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
    if (store) {
        PCCERT_CONTEXT ctx = nullptr;
        while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
            osCertificates.emplace_back(ctx->pbCertEncoded,
                                        ctx->pbCertEncoded + ctx->cbCertEncoded);
        }
        CertCloseStore(store, 0);
    }
    const auto counts = tls_internal::loadWindowsDefaultCAs(&ctx_->cacert, osCertificates);
    if (counts.windowsRoot + counts.bundled > 0) {
        logNotice() << "TlsClient: loaded " << counts.windowsRoot
                      << " CAs from Windows ROOT + " << counts.bundled << " bundled";
        return;
    }
#else
    const size_t before = countCerts(&ctx_->cacert);
    // POSIX: try well-known bundle paths in order. Works on macOS (/etc/ssl/cert.pem
    // is maintained by the OS), Linux distros, BSDs, Alpine, etc.
    static const char* kPaths[] = {
        "/etc/ssl/cert.pem",                       // macOS, OpenBSD, Alpine, FreeBSD
        "/etc/ssl/certs/ca-certificates.crt",      // Debian, Ubuntu, Raspberry Pi OS
        "/etc/pki/tls/certs/ca-bundle.crt",        // RHEL, Fedora, Amazon Linux
        "/etc/ssl/ca-bundle.pem",                  // OpenSUSE
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", // newer RHEL
        nullptr,
    };
    for (const char** p = kPaths; *p != nullptr; ++p) {
        struct stat st;
        if (stat(*p, &st) != 0) continue;
        if (st.st_size > kMaxCaPemBytes) {
            logWarning() << "TlsClient: skipping " << *p
                           << " (size " << st.st_size
                           << " > limit " << kMaxCaPemBytes << ")";
            continue;
        }
        std::ifstream f(*p);
        if (!f) continue;
        std::stringstream buf;
        buf << f.rdbuf();
        const std::string pem = buf.str();
        int ret = mbedtls_x509_crt_parse(
            &ctx_->cacert,
            reinterpret_cast<const unsigned char*>(pem.c_str()),
            pem.size() + 1);
        if (ret < 0) continue;  // try the next path
        const size_t loaded = countCerts(&ctx_->cacert) - before;
        if (loaded > 0) {
            logNotice() << "TlsClient: loaded " << loaded
                          << " CAs from " << *p;
            return;
        }
    }
    // Fallback: bundled Mozilla cacert.pem embedded at build time.
    const char* bundled = tls_internal::bundledCaPem();
    int ret = mbedtls_x509_crt_parse(
        &ctx_->cacert,
        reinterpret_cast<const unsigned char*>(bundled),
        std::strlen(bundled) + 1);
    if (ret >= 0) {
        const size_t loaded = countCerts(&ctx_->cacert) - before;
        if (loaded > 0) {
            logNotice() << "TlsClient: loaded " << loaded
                          << " CAs from bundled cacert.pem ("
                          << tls_internal::bundledCaBundleDate() << ")";
            return;
        }
    }
#endif

    logError() << "TlsClient: failed to load any default CA certificates. "
                 << "TLS handshakes will fail unless setCACertificate() or "
                 << "setVerifyNone() is called explicitly.";
}

// =============================================================================
// Connection Management
// =============================================================================
bool TlsClient::TlsTransport::connectSocket(State& st, const std::string& host, int port) {
    resetSslContext();
    ++tlsReceiveGeneration_;
    handshakePending_ = false;
    handshakeStarted_ = false;
    if (!Transport::connectSocket(st, host, port)) return false;
    handshakePending_ = true;
    if (!st.connectPending_) handshakeStart_ = std::chrono::steady_clock::now();
    // Rebind for every run; no State pointer is retained by the transport.
    mbedtls_ssl_set_bio(&ctx_->ssl, &st.socket_, mbedtls_net_send_callback,
                        mbedtls_net_recv_callback, nullptr);
    return true;
}

bool TlsClient::TlsTransport::setupHandshake(State& st, const AliveToken& alive) {
    int ret;
    ret = mbedtls_ssl_config_defaults(&ctx_->conf,
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        failHandshake(st, std::string("TLS config failed: ") + errBuf,
                      std::string("TLS config failed: ") + errBuf, ret, alive);
        return false;
    }

    // Certificate verification settings.
    // Default is REQUIRED: handshake aborts if the peer cert chain fails to verify.
    // VERIFY_OPTIONAL is unsafe here because we do not inspect
    // mbedtls_ssl_get_verify_result() after the handshake, so failures would be
    // silently ignored and we'd be talking TLS to an unauthenticated peer.
    if (verifyNone_) {
        mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_NONE);
    } else {
        // If the user never called setCACertificate*(), lazy-load default
        // trust anchors (Windows ROOT + bundle; POSIX bundle paths with
        // bundled fallback). Once-per-client.
        if (!caUserProvided_ && !caAutoLoadAttempted_) {
            ensureDefaultCAsLoaded();
        }
        mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&ctx_->conf, &ctx_->cacert, nullptr);
    }

    mbedtls_ssl_conf_rng(&ctx_->conf, mbedtls_ctr_drbg_random, &ctx_->ctr_drbg);

    // Setup SSL context
    ret = mbedtls_ssl_setup(&ctx_->ssl, &ctx_->conf);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        failHandshake(st, std::string("TLS setup failed: ") + errBuf,
                      std::string("TLS setup failed: ") + errBuf, ret, alive);
        return false;
    }

    // Set hostname (SNI)
    std::string sniHost = hostname_.empty() ? st.getRemoteHost() : hostname_;
    ret = mbedtls_ssl_set_hostname(&ctx_->ssl, sniHost.c_str());
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        failHandshake(st, std::string("TLS hostname set failed: ") + errBuf,
                      std::string("TLS hostname set failed: ") + errBuf, ret, alive);
        return false;
    }

    return true;
}

TlsClient::TlsTransport::HandshakeStep TlsClient::TlsTransport::performHandshake(State& st, const AliveToken& alive) {
    if (!handshakeStarted_) {
        if (!setupHandshake(st, alive)) return HandshakeStep::Stopped;
        handshakeStarted_ = true;
    }
    int ret;
    // Perform handshake step
    {
        std::scoped_lock lock(st.socketMutex_, tlsMutex_);
        if (!st.running_ || st.socket_ == INVALID_SOCKET) return HandshakeStep::Stopped;
        ret = mbedtls_ssl_handshake(&ctx_->ssl);
    }
    if (ret == 0) {
        return HandshakeStep::Done;
    } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return HandshakeStep::InProgress;
    } else {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        failHandshake(st, std::string("TLS handshake failed: ") + errBuf,
                      std::string("TLS Handshake failed: ") + errBuf, ret, alive);
        return HandshakeStep::Stopped;
    }
}

bool TlsClient::TlsTransport::failHandshake(State& st, const std::string& error, const std::string& connectMessage,
                              int code, const AliveToken& alive) {
    // A local disconnect() on another thread shut the socket down under
    // the handshake. It cleared running_ first and tears the connection
    // down itself: only a failure this thread ran into is handled here.
    if (!st.running_.exchange(false)) return false;

    // Tear the connection down before telling anyone: a listener that
    // reconnects from onError or onConnect runs inline, and a teardown
    // after it returned would take its new connection down.
    // teardown() leaves this run's thread owned by the State, so teardown
    // from another thread can still wait for its listener to return.
    teardown(st);
    st.notifyError(error, code);

    // An onError listener may have destroyed the client (an owner that
    // replaces it, as WebSocketClient::connect() does)
    if (!*alive) return false;

    // An onError listener may have started a newer attempt. That attempt
    // reports its own result: this one is not reported once the client
    // is connected or connecting again (running_ is set for every
    // attempt in progress, the pending connect and the handshake too).
    if (!st.connected_ && !st.running_ && !isConnecting(st)) {
        tc::TcpConnectEventArgs args;
        args.success = false;
        args.message = connectMessage;
        // The caller stops after this without reading the client: an
        // onConnect listener may destroy it too
        st.onConnect.notify(args);
    }
    return false;
}

bool TlsClient::TlsTransport::processNetworkStep(State& st, const AliveToken& alive) {
    const unsigned generation = tlsReceiveGeneration_;
    if (!st.processSendQueue(alive)) return false;
    // A thread whose connection was replaced does nothing more, not even
    // the pending connect or the handshake of the new one
    if (!st.running_ || tlsReceiveGeneration_ != generation) return true;

    // 1. Handle TCP connection pending
    if (st.connectPending_) {
        int error = 0;
        int state = st.checkPendingConnect(0, error);
        if (state == -2) return true;
        if (state < 0) return failHandshake(st, "TCP connection failed or timed out", "Connection failed", error, alive);
        if (state == 0) {
            if (st.useThread_) st.waitClientSocket(false, 50);
            return true;
        }
        handshakePending_ = true;
        handshakeStart_ = std::chrono::steady_clock::now();
    }

    // 2. Handle TLS handshake pending
    if (handshakePending_) {
        // The deadline counts from the TCP connect (setHandshakeTimeout()).
        // The peer may have accepted the connection and then said nothing.
        const float timeout = handshakeTimeout_;
        if (timeout > 0.0f &&
            std::chrono::steady_clock::now() - handshakeStart_ >=
                std::chrono::duration<float>(timeout)) {
            return failHandshake(st, "TLS handshake timeout", "TLS handshake timeout", 0, alive);
        }

        HandshakeStep step = performHandshake(st, alive);
        if (step == HandshakeStep::Stopped) return false;
        if (step == HandshakeStep::InProgress) {
            // With threads the socket is non-blocking during the handshake:
            // wait for the peer's data in short slices, so the deadline above
            // is checked again even when nothing arrives
            if (st.useThread_) st.waitClientSocket(false, 50);
            return true;
        }

        if (!st.running_ || tlsReceiveGeneration_ != generation || !*alive) return false;
        handshakePending_ = false;
        st.startSendChannel();
        st.connected_ = true;

        logNotice() << "TLS connected to " << st.getRemoteHost() << ":" << st.getRemotePort()
                      << " [" << getTlsVersion(st) << ", " << getCipherSuite(st) << "]";

        tc::TcpConnectEventArgs args;
        args.success = true;
        args.message = "TLS Connected";
        st.onConnect.notify(args);
        // An onConnect listener may have destroyed the client
        if (!*alive) return false;
    }

    // A listener on onConnect above may have reconnected and waited for the
    // new connection: connected_ is then the new connection's, and the new
    // receive thread owns tlsRecvBuf_. Stop before touching it.
    if (!st.connected_ || tlsReceiveGeneration_ != generation) return true;

    // Every reader uses its original State. The generation also prevents
    // an old notification from continuing into the transport's next run.
    while (st.connected_ && tlsReceiveGeneration_ == generation) {
        int ret;
        tc::TcpReceiveEventArgs args;
        {
            std::scoped_lock lock(st.socketMutex_, tlsMutex_);
            if (!st.running_ || st.socket_ == INVALID_SOCKET) break;
            // A reconnect from a writer listener may start a new run while
            // this reader returns. Own the bytes before releasing TLS's lock.
            if (tlsRecvBuf_.size() != st.receiveBufferSize_) tlsRecvBuf_.resize(st.receiveBufferSize_);
            ret = mbedtls_ssl_read(&ctx_->ssl, tlsRecvBuf_.data(), tlsRecvBuf_.size());
            if (ret > 0) {
                args.data.assign(reinterpret_cast<char*>(tlsRecvBuf_.data()),
                                 reinterpret_cast<char*>(tlsRecvBuf_.data()) + ret);
            }
        }

        if (ret > 0) {
            st.onReceive.notify(args);
            // An onReceive listener may have destroyed the client (an owner
            // that replaces it from a close it handles inline, say)
            if (!*alive) return false;
            if (!st.useThread_) break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // Connection closed. Report it only if this thread is the one
            // ending the connection. A local disconnect() clears running_
            // before its shutdown() wakes this read with EOF, and reports the
            // disconnect itself once it has joined this thread; reporting it
            // here as a remote close would let a reconnecting listener start
            // over while disconnect() is still joining this thread.
            if (st.running_.exchange(false)) {
                st.connected_ = false;
                if (!st.processSendQueue(alive)) return false;
                tc::TcpDisconnectEventArgs args;
                args.reason = "Connection closed by remote";
                args.wasClean = true;
                // This thread stops here, decided before notifying: a
                // listener may destroy, disconnect or reconnect the client,
                // so nothing of it is read after the notification (#262).
                st.onDisconnect.notify(args);
                return false;
            }
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        } else {
            // Error. As above: one caused by a local disconnect() is its to report
            if (st.running_.exchange(false)) {
                st.connected_ = false;
                if (!st.processSendQueue(alive)) return false;
                char errBuf[256];
                mbedtls_strerror(ret, errBuf, sizeof(errBuf));
                tc::TcpDisconnectEventArgs args;
                args.reason = std::string("TLS error: ") + errBuf;
                args.wasClean = false;
                // As above: stop without reading the client again
                st.onDisconnect.notify(args);
                return false;
            }
            break;
        }
    }
    return true;
}

// Free and re-initialise the SSL context and config (for the next connection)
void TlsClient::TlsTransport::resetSslContext() {
    std::lock_guard<std::mutex> lock(tlsMutex_);
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        mbedtls_ssl_config_free(&ctx_->conf);
        mbedtls_ssl_init(&ctx_->ssl);
        mbedtls_ssl_config_init(&ctx_->conf);
    }
}

// End a failed handshake before delivering its events; the State retains the worker.
void TlsClient::TlsTransport::teardown(State& st) {
    st.running_ = false;
    st.connectPending_ = false;
    handshakePending_ = false;
    st.updateListener_.disconnect();
    st.closeClientSocket();
    resetSslContext();
}

int TlsClient::TlsTransport::writeSendStep(State& st, const void* data, size_t size, bool& forWrite, int& error) {
    std::scoped_lock lock(st.socketMutex_, tlsMutex_);
    if (!st.connected_ || st.socket_ == INVALID_SOCKET) return -2;
    int ret = mbedtls_ssl_write(&ctx_->ssl, static_cast<const unsigned char*>(data), size);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        forWrite = ret == MBEDTLS_ERR_SSL_WANT_WRITE;
        return -1;
    }
    if (ret < 0) { error = ret; return -2; }
    return ret;
}

// =============================================================================
// TLS Information
// =============================================================================
std::string TlsClient::TlsTransport::getCipherSuite(const State& st) const {
    if (!st.connected_ || !ctx_) return "";
    const char* suite = mbedtls_ssl_get_ciphersuite(&ctx_->ssl);
    return suite ? suite : "";
}

std::string TlsClient::TlsTransport::getTlsVersion(const State& st) const {
    if (!st.connected_ || !ctx_) return "";
    const char* ver = mbedtls_ssl_get_version(&ctx_->ssl);
    return ver ? ver : "";
}

TlsClient::TlsClient() : transport_(std::make_shared<TlsTransport>()) { setTransport(transport_); }
// TcpClient closes delivery and joins/retains the run before releasing its transport.
TlsClient::~TlsClient() = default;
bool TlsClient::setCACertificate(const std::string& pem) { return transport_->setCACertificate(pem); }
bool TlsClient::setCACertificateFile(const std::string& path) { return transport_->setCACertificateFile(path); }
void TlsClient::setVerifyNone() { transport_->verifyNone_ = true; }
void TlsClient::setHostname(const std::string& hostname) { transport_->hostname_ = hostname; }
void TlsClient::setHandshakeTimeout(float seconds) { transport_->handshakeTimeout_ = seconds > 0 ? seconds : 0; }
bool TlsClient::connect(const std::string& host, int port) { return TcpClient::connect(host, port); }
void TlsClient::disconnect() { TcpClient::disconnect(); }
bool TlsClient::isConnecting() const { return TcpClient::isConnecting(); }
bool TlsClient::send(const void* data, size_t size) { return TcpClient::send(data, size); }
bool TlsClient::send(const std::vector<char>& data) { return TcpClient::send(data); }
bool TlsClient::send(const std::string& data) { return TcpClient::send(data); }
void TlsClient::processNetwork() { TcpClient::processNetwork(); }
std::string TlsClient::getCipherSuite() const { return transport_->getCipherSuite(*state()); }
std::string TlsClient::getTlsVersion() const { return transport_->getTlsVersion(*state()); }
std::chrono::steady_clock::time_point TlsClient::getTcpConnectTime() const { return transport_->handshakeStart_; }

}  // namespace tcx::tls
