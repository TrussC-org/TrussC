// =============================================================================
// tcTlsClient.h - TLS クライアントソケット
// TcpClient を継承し、mbedTLS で暗号化通信を行う
// =============================================================================
#pragma once

#include "tcTcpClient.h"
#include <string>

// 前方宣言（mbedtls ヘッダーを公開しない）
struct mbedtls_ssl_context;
struct mbedtls_ssl_config;
struct mbedtls_x509_crt;
struct mbedtls_ctr_drbg_context;
struct mbedtls_entropy_context;

namespace trussc {

// =============================================================================
// TlsClient クラス（TcpClient を継承）
// =============================================================================
class TlsClient : public TcpClient {
public:
    // -------------------------------------------------------------------------
    // コンストラクタ / デストラクタ
    // -------------------------------------------------------------------------
    TlsClient();
    ~TlsClient() override;

    // コピー禁止
    TlsClient(const TlsClient&) = delete;
    TlsClient& operator=(const TlsClient&) = delete;

    // -------------------------------------------------------------------------
    // TLS 設定
    // -------------------------------------------------------------------------

    // CA 証明書を設定（PEM形式の文字列）
    bool setCACertificate(const std::string& pemData);

    // CA 証明書をファイルから読み込み
    bool setCACertificateFile(const std::string& path);

    // サーバー証明書の検証を無効化（テスト用、本番では非推奨）
    void setVerifyNone();

    // ホスト名検証を設定（デフォルトは接続先ホスト名）
    void setHostname(const std::string& hostname);

    // -------------------------------------------------------------------------
    // 接続管理（TcpClient をオーバーライド）
    // -------------------------------------------------------------------------

    // サーバーに接続（TCP接続 + TLSハンドシェイク）
    bool connect(const std::string& host, int port) override;

    // 接続を切断
    void disconnect() override;

    // -------------------------------------------------------------------------
    // データ送受信（TcpClient をオーバーライド）
    // -------------------------------------------------------------------------

    // データを送信（TLS暗号化）
    bool send(const void* data, size_t size) override;
    bool send(const std::vector<char>& data) override;
    bool send(const std::string& message) override;

    // -------------------------------------------------------------------------
    // TLS 情報取得
    // -------------------------------------------------------------------------

    // 接続中の暗号スイート名
    std::string getCipherSuite() const;

    // TLS バージョン文字列
    std::string getTlsVersion() const;

private:
    // mbedTLS コンテキスト（PIMPL パターン）
    struct TlsContext;
    TlsContext* ctx_ = nullptr;

    std::string hostname_;
    bool verifyNone_ = false;

    // TLS ハンドシェイク実行
    bool performHandshake();

    // 受信スレッド（TLS用）
    void tlsReceiveThreadFunc();

    std::thread tlsReceiveThread_;
};

} // namespace trussc

namespace tc = trussc;
