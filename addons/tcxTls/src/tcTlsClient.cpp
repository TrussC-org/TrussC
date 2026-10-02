// =============================================================================
// tcTlsClient.cpp - TLS Client Socket Implementation
// =============================================================================

#include "tcTlsClient.h"
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
TlsClient::TlsClient() {
    ctx_ = new TlsContext();

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

TlsClient::~TlsClient() {
    // A receive thread whose listener is destroying this client checks this
    // once the notification returns, and stops without reading the client
    *alive_ = false;
    // Disconnect without onDisconnect: a listener that reconnects would
    // reconnect a client that is going away.
    disconnectImpl(false);
    delete ctx_;
}

// =============================================================================
// TLS Configuration
// =============================================================================
bool TlsClient::setCACertificate(const std::string& pemData) {
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

bool TlsClient::setCACertificateFile(const std::string& path) {
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

void TlsClient::setVerifyNone() {
    verifyNone_ = true;
}

void TlsClient::setHostname(const std::string& hostname) {
    hostname_ = hostname;
}

void TlsClient::setHandshakeTimeout(float seconds) {
    handshakeTimeout_ = seconds > 0.0f ? seconds : 0.0f;
}

// =============================================================================
// Default CA Loading (lazy)
// =============================================================================
void TlsClient::ensureDefaultCAsLoaded() {
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
bool TlsClient::connect(const std::string& host, int port) {
    // Disconnect if already connected
    if (connected_ || running_ || connectPending_ || handshakePending_) {
        disconnect();
    }

    // Release what is left before starting over.
    //  - After the peer closed the connection (or a TLS error ended it) the
    //    flags above are all clear, but the socket is still open and the
    //    finished receive thread still joinable: creating the new socket
    //    would overwrite and leak the old one.
    //  - The disconnect() above fired onDisconnect inline, and a listener may
    //    have reconnected from it. This call came first and overrules that
    //    connection: close it without another notification. running_ is
    //    cleared and the socket shut down before the join, so its receive
    //    thread, blocked in the handshake or a read, wakes up and its failure
    //    loses the exchange and reports nothing.
    running_ = false;
    connectPending_ = false;
    handshakePending_ = false;
    updateListener_.disconnect();
    closeSocket();

    // Ensure the previous receive thread has finished. A listener on that
    // thread (onDisconnect, say) that reconnects cannot join it: the client
    // keeps it instead, and the next connect() or disconnect() on another
    // thread, or the destructor, joins it. Its loops (processNetwork()'s
    // receive loop, then tlsReceiveThreadFunc()'s) end on their own once
    // running_ is cleared or the new receive thread has taken over.
    tlsKeptThreads_.release(tlsReceiveThread_);
    if (connected_.exchange(false)) {
        logWarning() << "TlsClient: connect() closes the connection an onDisconnect listener opened";
    }
    // A thread an earlier listener's connect() or disconnect() let go of
    // has been told to stop: wait for it before the SSL context is reset
    // for the new connection. The calling thread itself, if kept, stays.
    tlsKeptThreads_.joinOthers();

    // Reset SSL context (clear previous connection state)
    resetSslContext();

    // This connection's generation, taken before running_ is set for it. A
    // receive thread that a listener's disconnect() let go of may still be
    // running: it checks the generation together with running_, and has to
    // see the new generation by the time it can see running_ set, or it
    // handshakes and reads on the new connection next to the new receive
    // thread (or, without threads, next to the update event).
    const unsigned generation = ++tlsReceiveGeneration_;

    // Create socket
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (socket_ == INVALID_SOCKET) {
        notifyError("Failed to create socket", WSAGetLastError());
        return false;
    }
#else
    if (socket_ < 0) {
        notifyError("Failed to create socket", errno);
        return false;
    }
#endif

    // A send racing the peer's close must fail, not raise SIGPIPE
    tc::internal::setNoSigpipe(socket_);

    // Set non-blocking if not using threads
    if (!useThread_) {
        setBlocking(false);
    }

    // Resolve hostname
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int ret = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0) {
        // Clean up before notifying: an onError listener that reconnects
        // must not have its new socket closed after it returns
        closeSocket();
        notifyError("Failed to resolve host: " + host, ret);
        return false;
    }

    // TCP connection
    ret = ::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    remoteHost_ = host;
    remotePort_ = port;
    handshakeStarted_ = false;

    if (ret == SOCKET_ERROR) {
        int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
        if (err == WSAEWOULDBLOCK) {
#else
        if (err == EINPROGRESS) {
#endif
            // Async connection started
            connectPending_ = true;
            running_ = true;
        } else {
            // Clean up before notifying (see above)
            closeSocket();
            notifyError("Failed to connect to " + host + ":" + std::to_string(port), err);
            return false;
        }
    } else {
        // TCP Connected immediately
        running_ = true;
        handshakePending_ = true;
        handshakeStart_ = std::chrono::steady_clock::now();
        logNotice() << "TCP connected to " << host << ":" << port << ", starting TLS handshake...";
    }

    if (running_) {
        if (useThread_) {
            // Thread mode: the TCP connect above was blocking, so the
            // handshake is next. It runs on a non-blocking socket and waits
            // for data in short slices (processNetworkImpl()), so its
            // deadline is checked while the peer stays silent. The socket
            // goes back to blocking once the handshake is done.
            setBlocking(false);
            tlsReceiveThread_ = std::thread(&TlsClient::tlsReceiveThreadFunc, this,
                                            generation, alive_);
        } else {
            // Register update listener for async connect/handshake/recv
            updateListener_ = events().update.listen(this, &TlsClient::processNetwork);
        }
    }

    return true;
}

TlsClient::HandshakeStep TlsClient::performHandshake(const AliveToken& alive) {
    int ret;

    // Initialize SSL configuration if not already done
    if (!handshakeStarted_) {
        ret = mbedtls_ssl_config_defaults(&ctx_->conf,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS config failed: ") + errBuf, ret);
            return *alive ? HandshakeStep::InProgress : HandshakeStep::Stopped;
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
            notifyError(std::string("TLS setup failed: ") + errBuf, ret);
            return *alive ? HandshakeStep::InProgress : HandshakeStep::Stopped;
        }

        // Set hostname (SNI)
        std::string sniHost = hostname_.empty() ? remoteHost_ : hostname_;
        ret = mbedtls_ssl_set_hostname(&ctx_->ssl, sniHost.c_str());
        if (ret != 0) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS hostname set failed: ") + errBuf, ret);
            return *alive ? HandshakeStep::InProgress : HandshakeStep::Stopped;
        }

        // Set BIO callbacks
        mbedtls_ssl_set_bio(&ctx_->ssl, &socket_,
                             mbedtls_net_send_callback,
                             mbedtls_net_recv_callback, nullptr);
        
        handshakeStarted_ = true;
    }

    // Perform handshake step
    ret = mbedtls_ssl_handshake(&ctx_->ssl);
    if (ret == 0) {
        return HandshakeStep::Done;
    } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return HandshakeStep::InProgress;
    } else {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        failHandshake(std::string("TLS handshake failed: ") + errBuf,
                      std::string("TLS Handshake failed: ") + errBuf, ret, alive);
        return HandshakeStep::Stopped;
    }
}

bool TlsClient::failHandshake(const std::string& error, const std::string& connectMessage,
                              int code, const AliveToken& alive) {
    // A local disconnect() on another thread shut the socket down under
    // the handshake. It cleared running_ first and tears the connection
    // down itself: only a failure this thread ran into is handled here.
    if (!running_.exchange(false)) return false;

    // Tear the connection down before telling anyone: a listener that
    // reconnects from onError or onConnect runs inline, and a teardown
    // after it returned would take its new connection down. Not
    // disconnect(), which on this thread lets go of it. teardown() leaves
    // the thread joinable, so disconnect() and the destructor still wait
    // for it; only a listener's own connect() or disconnect() lets go of it
    // (the client keeps it for a later join).
    teardown();
    notifyError(error, code);

    // An onError listener may have destroyed the client (an owner that
    // replaces it, as WebSocketClient::connect() does)
    if (!*alive) return false;

    // An onError listener may have started a newer attempt. That attempt
    // reports its own result: this one is not reported once the client
    // is connected or connecting again (running_ is set for every
    // attempt in progress, the pending connect and the handshake too).
    if (!connected_ && !running_) {
        tc::TcpConnectEventArgs args;
        args.success = false;
        args.message = connectMessage;
        // The caller stops after this without reading the client: an
        // onConnect listener may destroy it too
        onConnect.notify(args);
    }
    return false;
}

void TlsClient::processNetwork() {
    // Without threads (driven by the update event) no generation is started;
    // the current one is this call's. The result is not needed: a stop means
    // the connection ended and the update listener is gone. The token is
    // copied first: a listener may destroy the client.
    AliveToken alive = alive_;
    processNetworkImpl(tlsReceiveGeneration_, alive);
}

// Wait until the socket has data to read, for at most ms milliseconds
static void waitReadable(
#ifdef _WIN32
    SOCKET fd,
#else
    int fd,
#endif
    int ms) {
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timeval tv = {0, ms * 1000};
    select(0, &readfds, NULL, NULL, &tv);
#else
    if (fd < 0) return;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    poll(&pfd, 1, ms);
#endif
}

// generation: the receive thread's own (the generation it was started with)
bool TlsClient::processNetworkImpl(unsigned generation, const AliveToken& alive) {
    // A thread whose connection was replaced does nothing more, not even
    // the pending connect or the handshake of the new one
    if (!running_ || tlsReceiveGeneration_ != generation) return true;

    // 1. Handle TCP connection pending
    if (connectPending_) {
        // Re-use TcpClient's connect check logic (simplified here for brevity, 
        // ideally we'd have a shared checkConnect() method)
#ifdef _WIN32
        struct fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(socket_, &writefds);
        struct timeval tv = {0, 0};
        if (select(0, NULL, &writefds, NULL, &tv) > 0) {
#else
        struct pollfd pfd;
        pfd.fd = socket_;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 0) > 0) {
#endif
            connectPending_ = false;
            handshakePending_ = true;
            handshakeStart_ = std::chrono::steady_clock::now();
            logNotice() << "TCP connected (async), starting TLS handshake...";
        } else {
            return true; // Still connecting
        }
    }

    // 2. Handle TLS handshake pending
    if (handshakePending_) {
        // The deadline counts from the TCP connect (setHandshakeTimeout()).
        // The peer may have accepted the connection and then said nothing.
        const float timeout = handshakeTimeout_;
        if (timeout > 0.0f &&
            std::chrono::steady_clock::now() - handshakeStart_ >=
                std::chrono::duration<float>(timeout)) {
            return failHandshake("TLS handshake timeout", "TLS handshake timeout", 0, alive);
        }

        HandshakeStep step = performHandshake(alive);
        if (step == HandshakeStep::Stopped) return false;
        if (step == HandshakeStep::InProgress) {
            // With threads the socket is non-blocking during the handshake:
            // wait for the peer's data in short slices, so the deadline above
            // is checked again even when nothing arrives
            if (useThread_) waitReadable(socket_, 50);
            return true;
        }

        handshakePending_ = false;
        // With threads, back to blocking reads for the connection
        if (useThread_) setBlocking(true);
        connected_ = true;

        logNotice() << "TLS connected to " << remoteHost_ << ":" << remotePort_
                      << " [" << getTlsVersion() << ", " << getCipherSuite() << "]";

        tc::TcpConnectEventArgs args;
        args.success = true;
        args.message = "TLS Connected";
        onConnect.notify(args);
        // An onConnect listener may have destroyed the client
        if (!*alive) return false;
    }

    // A listener on onConnect above may have reconnected and waited for the
    // new connection: connected_ is then the new connection's, and the new
    // receive thread owns tlsRecvBuf_. Stop before touching it.
    if (!connected_ || tlsReceiveGeneration_ != generation) return true;

    // 3. Handle data receive. The buffer is this client's own: every
    // client's receive thread runs this at the same time.
    if (tlsRecvBuf_.size() != receiveBufferSize_) {
        tlsRecvBuf_.resize(receiveBufferSize_);
    }

    // A listener on this thread that reconnects (an onReceive listener that
    // calls connect(), say) starts a new connection with its own receive
    // thread. The generation stops this loop instead of letting it go back
    // to reading the new connection next to that thread, sharing the SSL
    // context and tlsRecvBuf_ with it. It is the thread's own, passed in, not
    // read here: onConnect above may already have reconnected.
    while (connected_ && tlsReceiveGeneration_ == generation) {
        int ret = mbedtls_ssl_read(&ctx_->ssl, tlsRecvBuf_.data(), tlsRecvBuf_.size());

        if (ret > 0) {
            tc::TcpReceiveEventArgs args;
            args.data.assign(reinterpret_cast<char*>(tlsRecvBuf_.data()),
                            reinterpret_cast<char*>(tlsRecvBuf_.data()) + ret);
            onReceive.notify(args);
            // An onReceive listener may have destroyed the client (an owner
            // that replaces it from a close it handles inline, say)
            if (!*alive) return false;
            if (!useThread_) break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // Connection closed. Report it only if this thread is the one
            // ending the connection. A local disconnect() clears running_
            // before its shutdown() wakes this read with EOF, and reports the
            // disconnect itself once it has joined this thread; reporting it
            // here as a remote close would let a reconnecting listener start
            // over while disconnect() is still joining this thread.
            if (running_.exchange(false)) {
                connected_ = false;
                tc::TcpDisconnectEventArgs args;
                args.reason = "Connection closed by remote";
                args.wasClean = true;
                // This thread stops here, decided before notifying: a
                // listener may destroy, disconnect or reconnect the client,
                // so nothing of it is read after the notification (#262).
                onDisconnect.notify(args);
                return false;
            }
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        } else {
            // Error. As above: one caused by a local disconnect() is its to report
            if (running_.exchange(false)) {
                connected_ = false;
                char errBuf[256];
                mbedtls_strerror(ret, errBuf, sizeof(errBuf));
                tc::TcpDisconnectEventArgs args;
                args.reason = std::string("TLS error: ") + errBuf;
                args.wasClean = false;
                // As above: stop without reading the client again
                onDisconnect.notify(args);
                return false;
            }
            break;
        }
    }
    return true;
}

void TlsClient::disconnect() {
    disconnectImpl(true);
}

// disconnect() with notify, the destructor without
void TlsClient::disconnectImpl(bool notify) {
    running_ = false;
    connectPending_ = false;
    handshakePending_ = false;
    updateListener_.disconnect();

    // Send TLS close notification
    if (ctx_ && connected_) {
        mbedtls_ssl_close_notify(&ctx_->ssl);
    }

    closeSocket();

    // Wait for receive thread to finish. On that thread itself (a
    // listener), keep it for a later join from another thread. Then join
    // what earlier calls kept.
    tlsKeptThreads_.release(tlsReceiveThread_);
    tlsKeptThreads_.joinOthers();

    // Fully reset SSL context and config (for reconnection). Before the
    // notification below: a listener that reconnects from it starts a new
    // handshake on this context, which a reset afterwards would free under
    // the new receive thread.
    resetSslContext();

    // Last: nothing above may run after a listener's reconnect. The receive
    // thread reports only a close it ran into itself (running_ still set).
    // The EOF that the shutdown() above wakes it with is this call's own,
    // and is reported here, once, after the join.
    if (connected_.exchange(false) && notify) {
        tc::TcpDisconnectEventArgs args;
        args.reason = "Disconnected by client";
        args.wasClean = true;
        onDisconnect.notify(args);
    }
}

// Shut down and close the socket, if there is one
void TlsClient::closeSocket() {
#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
#else
    if (socket_ >= 0) {
        shutdown(socket_, SHUT_RDWR);
        close(socket_);
        socket_ = -1;
    }
#endif
}

// Free and re-initialise the SSL context and config (for the next connection)
void TlsClient::resetSslContext() {
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        mbedtls_ssl_config_free(&ctx_->conf);
        mbedtls_ssl_init(&ctx_->ssl);
        mbedtls_ssl_config_init(&ctx_->conf);
    }
}

// Clear the connection flags, close the socket and reset the SSL context.
// tlsReceiveThread_ is left alone (see the header).
void TlsClient::teardown() {
    running_ = false;
    connectPending_ = false;
    handshakePending_ = false;
    updateListener_.disconnect();
    closeSocket();
    resetSslContext();
}

// =============================================================================
// Data Send/Receive
// =============================================================================
bool TlsClient::send(const void* data, size_t size) {
    if (!connected_) {
        notifyError("Not connected");
        return false;
    }

    std::lock_guard<std::mutex> lock(sendMutex_);

    const unsigned char* ptr = static_cast<const unsigned char*>(data);
    size_t remaining = size;

    while (remaining > 0) {
        int ret = mbedtls_ssl_write(&ctx_->ssl, ptr, remaining);
        if (ret < 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) continue;
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS send failed: ") + errBuf, ret);
            return false;
        }
        ptr += ret;
        remaining -= ret;
    }

    return true;
}

bool TlsClient::send(const std::vector<char>& data) {
    return send(data.data(), data.size());
}

bool TlsClient::send(const std::string& message) {
    return send(message.data(), message.size());
}

// =============================================================================
// TLS Receive Thread
// =============================================================================
void TlsClient::tlsReceiveThreadFunc(unsigned generation, AliveToken alive) {
    // running_ alone cannot end this loop when a listener on this thread
    // reconnects: connect() lets go of this thread (the client keeps it for
    // a later join), starts the new connection's own, and running_ is true
    // again for that one. The generation says which
    // thread is current (processNetwork()'s receive loop checks it as well).
    // processNetworkImpl() returns false once it reported the end of the
    // connection or of the handshake, or a listener destroyed the client:
    // the thread then ends without reading the client again.
    while (running_ && tlsReceiveGeneration_ == generation) {
        if (!processNetworkImpl(generation, alive)) return;
        if (running_ && tlsReceiveGeneration_ == generation) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// =============================================================================
// TLS Information
// =============================================================================
std::string TlsClient::getCipherSuite() const {
    if (!connected_ || !ctx_) return "";
    const char* suite = mbedtls_ssl_get_ciphersuite(&ctx_->ssl);
    return suite ? suite : "";
}

std::string TlsClient::getTlsVersion() const {
    if (!connected_ || !ctx_) return "";
    const char* ver = mbedtls_ssl_get_version(&ctx_->ssl);
    return ver ? ver : "";
}

}  // namespace tcx::tls
