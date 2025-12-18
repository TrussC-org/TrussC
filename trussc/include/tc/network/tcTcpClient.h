// =============================================================================
// tcTcpClient.h - TCP クライアントソケット
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
#endif

namespace trussc {

// =============================================================================
// イベント引数
// =============================================================================

// 接続完了イベント
struct TcpConnectEventArgs {
    bool success = false;
    std::string message;
};

// データ受信イベント
struct TcpReceiveEventArgs {
    std::vector<char> data;
};

// 切断イベント
struct TcpDisconnectEventArgs {
    std::string reason;
    bool wasClean = true;  // 正常切断かどうか
};

// エラーイベント
struct TcpErrorEventArgs {
    std::string message;
    int errorCode = 0;
};

// =============================================================================
// TcpClient クラス（基底クラス - TlsClient の親）
// =============================================================================
class TcpClient {
public:
    // -------------------------------------------------------------------------
    // イベント
    // -------------------------------------------------------------------------
    Event<TcpConnectEventArgs> onConnect;       // 接続完了時
    Event<TcpReceiveEventArgs> onReceive;       // データ受信時
    Event<TcpDisconnectEventArgs> onDisconnect; // 切断時
    Event<TcpErrorEventArgs> onError;           // エラー発生時

    // -------------------------------------------------------------------------
    // コンストラクタ / デストラクタ
    // -------------------------------------------------------------------------
    TcpClient();
    virtual ~TcpClient();

    // コピー禁止
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // ムーブは許可
    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;

    // -------------------------------------------------------------------------
    // 接続管理（virtual - TlsClient でオーバーライド可能）
    // -------------------------------------------------------------------------

    // サーバーに接続（ブロッキング）
    virtual bool connect(const std::string& host, int port);

    // サーバーに非同期接続（バックグラウンドで接続、onConnectで通知）
    virtual void connectAsync(const std::string& host, int port);

    // 接続を切断
    virtual void disconnect();

    // 接続中かどうか
    virtual bool isConnected() const;

    // -------------------------------------------------------------------------
    // データ送受信（virtual - TlsClient でオーバーライド可能）
    // -------------------------------------------------------------------------

    // データを送信
    virtual bool send(const void* data, size_t size);
    virtual bool send(const std::vector<char>& data);
    virtual bool send(const std::string& message);

    // -------------------------------------------------------------------------
    // 設定
    // -------------------------------------------------------------------------

    // 受信バッファサイズを設定
    void setReceiveBufferSize(size_t size);

    // ブロッキングモードを設定
    void setBlocking(bool blocking);

    // -------------------------------------------------------------------------
    // 情報取得
    // -------------------------------------------------------------------------

    // 接続先ホスト名
    std::string getRemoteHost() const;

    // 接続先ポート
    int getRemotePort() const;

protected:
    // 継承先からアクセス可能
    void notifyError(const std::string& msg, int code = 0);

#ifdef _WIN32
    SOCKET socket_ = INVALID_SOCKET;
#else
    int socket_ = -1;
#endif

    std::string remoteHost_;
    int remotePort_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    size_t receiveBufferSize_ = 65536;
    std::mutex sendMutex_;

private:
    void receiveThreadFunc();
    void connectThreadFunc(const std::string& host, int port);

    std::thread receiveThread_;
    std::thread connectThread_;

    static std::atomic<int> instanceCount_;
    static void initWinsock();
    static void cleanupWinsock();
};

} // namespace trussc

namespace tc = trussc;
