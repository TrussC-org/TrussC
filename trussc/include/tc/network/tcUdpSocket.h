#pragma once

// =============================================================================
// tcUdpSocket.h - UDP ソケット
// =============================================================================

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "../events/tcEvent.h"
#include "../events/tcEventListener.h"
#include "../utils/tcLog.h"

// プラットフォーム固有のソケット型
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketHandle = SOCKET;
    #define INVALID_SOCKET_HANDLE INVALID_SOCKET
#else
    using SocketHandle = int;
    #define INVALID_SOCKET_HANDLE (-1)
#endif

namespace trussc {

// ---------------------------------------------------------------------------
// UDP 受信イベント引数
// ---------------------------------------------------------------------------
struct UdpReceiveEventArgs {
    std::vector<char> data;     // 受信データ
    std::string remoteHost;     // 送信元ホスト
    int remotePort = 0;         // 送信元ポート
};

// ---------------------------------------------------------------------------
// UDP エラーイベント引数
// ---------------------------------------------------------------------------
struct UdpErrorEventArgs {
    std::string message;        // エラーメッセージ
    int errorCode = 0;          // エラーコード
};

// ---------------------------------------------------------------------------
// UdpSocket - UDP ソケットクラス
// ---------------------------------------------------------------------------
class UdpSocket {
public:
    // イベント
    Event<UdpReceiveEventArgs> onReceive;   // データ受信時
    Event<UdpErrorEventArgs> onError;       // エラー発生時

    UdpSocket();
    ~UdpSocket();

    // コピー禁止
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // ムーブ
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // -------------------------------------------------------------------------
    // 設定
    // -------------------------------------------------------------------------

    // ソケットを作成（通常は bind/connect で自動作成されるので呼ばなくてOK）
    bool create();

    // 受信用にポートをバインド
    // startReceiving=true で自動的に受信スレッドを開始
    bool bind(int port, bool startReceiving = true);

    // 送信先を設定（設定後は send(data, size) で送信可能）
    bool connect(const std::string& host, int port);

    // ソケットを閉じる
    void close();

    // -------------------------------------------------------------------------
    // 送受信
    // -------------------------------------------------------------------------

    // 指定したホスト・ポートに送信
    bool sendTo(const std::string& host, int port, const void* data, size_t size);
    bool sendTo(const std::string& host, int port, const std::string& message);

    // connect() で設定した送信先に送信
    bool send(const void* data, size_t size);
    bool send(const std::string& message);

    // 同期受信（ブロッキング）- イベントを使わない場合用
    // 戻り値: 受信バイト数、エラー時は -1
    int receive(void* buffer, size_t bufferSize);
    int receive(void* buffer, size_t bufferSize, std::string& remoteHost, int& remotePort);

    // -------------------------------------------------------------------------
    // 受信スレッド制御
    // -------------------------------------------------------------------------

    // 受信スレッドを開始（bind後に自動で呼ばれる）
    void startReceiving();

    // 受信スレッドを停止
    void stopReceiving();

    // 受信中か
    bool isReceiving() const { return receiving_.load(); }

    // -------------------------------------------------------------------------
    // 設定・情報
    // -------------------------------------------------------------------------

    // ノンブロッキングモード設定
    bool setNonBlocking(bool nonBlocking);

    // ブロードキャスト許可
    bool setBroadcast(bool enable);

    // 再利用許可（bind前に設定）
    bool setReuseAddress(bool enable);

    // 受信バッファサイズ設定
    bool setReceiveBufferSize(int size);

    // 送信バッファサイズ設定
    bool setSendBufferSize(int size);

    // 受信タイムアウト設定（ミリ秒、0=無限）
    bool setReceiveTimeout(int timeoutMs);

    // バインドしているポートを取得
    int getLocalPort() const { return localPort_; }

    // ソケットが有効か
    bool isValid() const { return socket_ != INVALID_SOCKET_HANDLE; }

    // 接続先情報
    const std::string& getConnectedHost() const { return connectedHost_; }
    int getConnectedPort() const { return connectedPort_; }

private:
    void receiveThreadFunc();
    bool ensureSocket();
    void notifyError(const std::string& message, int code = 0);

    SocketHandle socket_ = INVALID_SOCKET_HANDLE;
    int localPort_ = 0;
    std::string connectedHost_;
    int connectedPort_ = 0;

    // 受信スレッド
    std::thread receiveThread_;
    std::atomic<bool> receiving_{false};
    std::atomic<bool> shouldStop_{false};

    // 受信バッファサイズ
    static constexpr size_t RECEIVE_BUFFER_SIZE = 65536;

    // Winsock 初期化（Windows用）
    static bool initWinsock();
    static bool winsockInitialized_;
};

} // namespace trussc

namespace tc = trussc;
