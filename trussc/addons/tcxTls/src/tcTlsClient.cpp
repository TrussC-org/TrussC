// =============================================================================
// tcTlsClient.cpp - TLS クライアントソケット実装
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
// TLS コンテキスト構造体（PIMPL）
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
// mbedTLS 用コールバック（ソケット送受信）
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
// コンストラクタ / デストラクタ
// =============================================================================
TlsClient::TlsClient() {
    ctx_ = new TlsContext();

    // 乱数生成器を初期化
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
// TLS 設定
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
// 接続管理
// =============================================================================
bool TlsClient::connect(const std::string& host, int port) {
    // 既存の接続があれば切断
    if (connected_ || running_) {
        disconnect();
    }

    // 前回の受信スレッドが終了していることを確認
    if (tlsReceiveThread_.joinable()) {
        tlsReceiveThread_.join();
    }

    // SSL コンテキストをリセット（前回の接続状態をクリア）
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        mbedtls_ssl_config_free(&ctx_->conf);
        mbedtls_ssl_init(&ctx_->ssl);
        mbedtls_ssl_config_init(&ctx_->conf);
    }

    // ソケット作成
#ifdef _WIN32
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        notifyError("Failed to create socket", WSAGetLastError());
        return false;
    }
#else
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ < 0) {
        notifyError("Failed to create socket", errno);
        return false;
    }
#endif

    // ホスト名解決
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int ret = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (ret != 0) {
        notifyError("Failed to resolve host: " + host, ret);
#ifdef _WIN32
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
#else
        close(socket_);
        socket_ = -1;
#endif
        return false;
    }

    // TCP 接続
    ret = ::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    if (ret != 0) {
#ifdef _WIN32
        notifyError("Failed to connect to " + host + ":" + std::to_string(port), WSAGetLastError());
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
#else
        notifyError("Failed to connect to " + host + ":" + std::to_string(port), errno);
        close(socket_);
        socket_ = -1;
#endif
        return false;
    }

    remoteHost_ = host;
    remotePort_ = port;

    tcLogNotice() << "TCP connected to " << host << ":" << port << ", starting TLS handshake...";

    // TLS ハンドシェイク
    if (!performHandshake()) {
#ifdef _WIN32
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
#else
        close(socket_);
        socket_ = -1;
#endif
        return false;
    }

    connected_ = true;
    running_ = true;

    // TLS 受信スレッド開始
    tlsReceiveThread_ = std::thread(&TlsClient::tlsReceiveThreadFunc, this);

    tcLogNotice() << "TLS connected to " << host << ":" << port
                  << " [" << getTlsVersion() << ", " << getCipherSuite() << "]";

    TcpConnectEventArgs args;
    args.success = true;
    args.message = "TLS Connected";
    onConnect.notify(args);

    return true;
}

bool TlsClient::performHandshake() {
    int ret;

    // SSL 設定を初期化
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

    // 証明書検証設定
    if (verifyNone_) {
        mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_NONE);
    } else {
        mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_ca_chain(&ctx_->conf, &ctx_->cacert, nullptr);
    }

    mbedtls_ssl_conf_rng(&ctx_->conf, mbedtls_ctr_drbg_random, &ctx_->ctr_drbg);

    // SSL コンテキストをセットアップ
    ret = mbedtls_ssl_setup(&ctx_->ssl, &ctx_->conf);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        notifyError(std::string("TLS setup failed: ") + errBuf, ret);
        return false;
    }

    // ホスト名設定（SNI）
    std::string sniHost = hostname_.empty() ? remoteHost_ : hostname_;
    ret = mbedtls_ssl_set_hostname(&ctx_->ssl, sniHost.c_str());
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        notifyError(std::string("TLS hostname set failed: ") + errBuf, ret);
        return false;
    }

    // BIO コールバック設定
    mbedtls_ssl_set_bio(&ctx_->ssl, &socket_,
                         mbedtls_net_send_callback,
                         mbedtls_net_recv_callback, nullptr);

    // ハンドシェイク実行
    while ((ret = mbedtls_ssl_handshake(&ctx_->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            notifyError(std::string("TLS handshake failed: ") + errBuf, ret);
            return false;
        }
    }

    return true;
}

void TlsClient::disconnect() {
    running_ = false;

    // TLS クローズ通知
    if (ctx_ && connected_) {
        mbedtls_ssl_close_notify(&ctx_->ssl);
    }

    // ソケットを閉じる
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

    // 受信スレッド終了待ち
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

    // SSL コンテキストと設定を完全にリセット（再接続のため）
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        mbedtls_ssl_config_free(&ctx_->conf);

        // 再初期化
        mbedtls_ssl_init(&ctx_->ssl);
        mbedtls_ssl_config_init(&ctx_->conf);
    }
}

// =============================================================================
// データ送受信
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
            if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
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
// TLS 受信スレッド
// =============================================================================
void TlsClient::tlsReceiveThreadFunc() {
    std::vector<unsigned char> buffer(receiveBufferSize_);

    while (running_) {
        int ret = mbedtls_ssl_read(&ctx_->ssl, buffer.data(), buffer.size());

        if (ret > 0) {
            TcpReceiveEventArgs args;
            args.data.assign(reinterpret_cast<char*>(buffer.data()),
                            reinterpret_cast<char*>(buffer.data()) + ret);
            onReceive.notify(args);
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // 接続が閉じられた
            running_ = false;
            connected_ = false;
            TcpDisconnectEventArgs args;
            args.reason = "Connection closed by remote";
            args.wasClean = true;
            onDisconnect.notify(args);
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            continue;
        } else {
            // エラー
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

// =============================================================================
// TLS 情報取得
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
