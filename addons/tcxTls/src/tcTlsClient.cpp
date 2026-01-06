// =============================================================================
// tcTlsClient.cpp - TLS Client Socket Implementation
// =============================================================================

#include "tcTlsClient.h"
#include "tc/utils/tcLog.h"

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace trussc {

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
    int ret = static_cast<int>(::send(fd, buf, len, 0));
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
        tcLogError() << "TlsClient: Failed to seed random generator: " << ret;
    }
}

TlsClient::~TlsClient() {
    disconnect();
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
        tcLogError() << "TlsClient: Failed to parse CA certificate: " << errBuf;
        return false;
    }
    return true;
}

bool TlsClient::setCACertificateFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        tcLogError() << "TlsClient: Failed to open CA certificate file: " << path;
        return false;
    }

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

// =============================================================================
// Connection Management
// =============================================================================
bool TlsClient::connect(const std::string& host, int port) {
    // Disconnect if already connected
    if (connected_ || running_ || connectPending_ || handshakePending_) {
        disconnect();
    }

    // Ensure previous receive thread has finished
    if (tlsReceiveThread_.joinable()) {
        tlsReceiveThread_.join();
    }

    // Reset SSL context (clear previous connection state)
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        mbedtls_ssl_config_free(&ctx_->conf);
        mbedtls_ssl_init(&ctx_->ssl);
        mbedtls_ssl_config_init(&ctx_->conf);
    }

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
        notifyError("Failed to resolve host: " + host, ret);
        CLOSE_SOCKET(socket_);
#ifdef _WIN32
        socket_ = INVALID_SOCKET;
#else
        socket_ = -1;
#endif
        return false;
    }

    // TCP connection
    ret = ::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    remoteHost_ = host;
    remotePort_ = port;

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
            notifyError("Failed to connect to " + host + ":" + std::to_string(port), err);
            CLOSE_SOCKET(socket_);
#ifdef _WIN32
            socket_ = INVALID_SOCKET;
#else
            socket_ = -1;
#endif
            return false;
        }
    } else {
        // TCP Connected immediately
        running_ = true;
        handshakePending_ = true;
        tcLogNotice() << "TCP connected to " << host << ":" << port << ", starting TLS handshake...";
    }

    if (running_) {
        if (useThread_) {
            // Thread mode: wait for TCP then handshake
            setBlocking(true);
            tlsReceiveThread_ = std::thread(&TlsClient::tlsReceiveThreadFunc, this);
        } else {
            // Register update listener for async connect/handshake/recv
            events().update.listen(updateListener_, this, &TlsClient::processNetwork);
        }
    }

    return true;
}

bool TlsClient::performHandshake() {
    int ret;

    // Initialize SSL configuration if not already done
    if (ctx_->conf.rng == nullptr) {
        ret = mbedtls_ssl_config_defaults(&ctx_->conf,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS config failed: ") + errBuf, ret);
            return false;
        }

        // Certificate verification settings
        if (verifyNone_) {
            mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_NONE);
        } else {
            mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
            mbedtls_ssl_conf_ca_chain(&ctx_->conf, &ctx_->cacert, nullptr);
        }

        mbedtls_ssl_conf_rng(&ctx_->conf, mbedtls_ctr_drbg_random, &ctx_->ctr_drbg);

        // Setup SSL context
        ret = mbedtls_ssl_setup(&ctx_->ssl, &ctx_->conf);
        if (ret != 0) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS setup failed: ") + errBuf, ret);
            return false;
        }

        // Set hostname (SNI)
        std::string sniHost = hostname_.empty() ? remoteHost_ : hostname_;
        ret = mbedtls_ssl_set_hostname(&ctx_->ssl, sniHost.c_str());
        if (ret != 0) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS hostname set failed: ") + errBuf, ret);
            return false;
        }

        // Set BIO callbacks
        mbedtls_ssl_set_bio(&ctx_->ssl, &socket_,
                             mbedtls_net_send_callback,
                             mbedtls_net_recv_callback, nullptr);
    }

    // Perform handshake step
    ret = mbedtls_ssl_handshake(&ctx_->ssl);
    if (ret == 0) {
        return true; // Handshake complete
    } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return false; // In progress
    } else {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        notifyError(std::string("TLS handshake failed: ") + errBuf, ret);
        disconnect(); // Terminate connection on error
        
        TcpConnectEventArgs args;
        args.success = false;
        args.message = std::string("TLS Handshake failed: ") + errBuf;
        onConnect.notify(args);
        return false;
    }
}

void TlsClient::processNetwork() {
    if (!running_) return;

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
            tcLogNotice() << "TCP connected (async), starting TLS handshake...";
        } else {
            return; // Still connecting
        }
    }

    // 2. Handle TLS handshake pending
    if (handshakePending_) {
        if (performHandshake()) {
            handshakePending_ = false;
            connected_ = true;
            
            tcLogNotice() << "TLS connected to " << remoteHost_ << ":" << remotePort_
                          << " [" << getTlsVersion() << ", " << getCipherSuite() << "]";

            TcpConnectEventArgs args;
            args.success = true;
            args.message = "TLS Connected";
            onConnect.notify(args);
        } else {
            return; // Still handshaking or failed (disconnect called inside)
        }
    }

    if (!connected_) return;

    // 3. Handle data receive
    static std::vector<unsigned char> buffer;
    if (buffer.size() != receiveBufferSize_) {
        buffer.resize(receiveBufferSize_);
    }

    while (connected_) {
        int ret = mbedtls_ssl_read(&ctx_->ssl, buffer.data(), buffer.size());

        if (ret > 0) {
            TcpReceiveEventArgs args;
            args.data.assign(reinterpret_cast<char*>(buffer.data()),
                            reinterpret_cast<char*>(buffer.data()) + ret);
            onReceive.notify(args);
            if (!useThread_) break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // Connection closed
            running_ = false;
            connected_ = false;
            TcpDisconnectEventArgs args;
            args.reason = "Connection closed by remote";
            args.wasClean = true;
            onDisconnect.notify(args);
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        } else {
            // Error
            if (running_) {
                running_ = false;
                connected_ = false;
                char errBuf[256];
                mbedtls_strerror(ret, errBuf, sizeof(errBuf));
                TcpDisconnectEventArgs args;
                args.reason = std::string("TLS error: ") + errBuf;
                args.wasClean = false;
                onDisconnect.notify(args);
            }
            break;
        }
    }
}

void TlsClient::disconnect() {
    running_ = false;
    connectPending_ = false;
    handshakePending_ = false;
    updateListener_.disconnect();

    // Send TLS close notification
    if (ctx_ && connected_) {
        mbedtls_ssl_close_notify(&ctx_->ssl);
    }

    // Close socket
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

    // Wait for receive thread to finish
    if (tlsReceiveThread_.joinable()) {
        tlsReceiveThread_.join();
    }

    if (connected_) {
        connected_ = false;
        TcpDisconnectEventArgs args;
        args.reason = "Disconnected by client";
        args.wasClean = true;
        onDisconnect.notify(args);
    }

    // Fully reset SSL context and config (for reconnection)
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        mbedtls_ssl_config_free(&ctx_->conf);

        // Reinitialize
        mbedtls_ssl_init(&ctx_->ssl);
        mbedtls_ssl_config_init(&ctx_->conf);
    }
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
void TlsClient::tlsReceiveThreadFunc() {
    while (running_) {
        processNetwork();
        if (running_) {
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

} // namespace trussc
