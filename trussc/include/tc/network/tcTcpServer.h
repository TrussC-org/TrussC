// =============================================================================
// tcTcpServer.h - TCP サーバーソケット
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
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
// 接続済みクライアント情報
// =============================================================================
struct TcpServerClient {
    int id;                 // クライアントID（サーバーが割り当て）
    std::string host;       // クライアントのIPアドレス
    int port;               // クライアントのポート

#ifdef _WIN32
    SOCKET socket;
#else
    int socket;
#endif
};

// =============================================================================
// イベント引数
// =============================================================================

// クライアント接続イベント
struct TcpClientConnectEventArgs {
    int clientId;
    std::string host;
    int port;
};

// クライアントからデータ受信イベント
struct TcpServerReceiveEventArgs {
    int clientId;
    std::vector<char> data;
};

// クライアント切断イベント
struct TcpClientDisconnectEventArgs {
    int clientId;
    std::string reason;
    bool wasClean;
};

// サーバーエラーイベント
struct TcpServerErrorEventArgs {
    std::string message;
    int errorCode = 0;
    int clientId = -1;  // -1 = サーバー自体のエラー
};

// =============================================================================
// TcpServer クラス
// =============================================================================
class TcpServer {
public:
    // -------------------------------------------------------------------------
    // イベント
    // -------------------------------------------------------------------------
    Event<TcpClientConnectEventArgs> onClientConnect;       // クライアント接続時
    Event<TcpServerReceiveEventArgs> onReceive;             // データ受信時
    Event<TcpClientDisconnectEventArgs> onClientDisconnect; // クライアント切断時
    Event<TcpServerErrorEventArgs> onError;                 // エラー発生時

    // -------------------------------------------------------------------------
    // コンストラクタ / デストラクタ
    // -------------------------------------------------------------------------
    TcpServer();
    ~TcpServer();

    // コピー禁止
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // -------------------------------------------------------------------------
    // サーバー管理
    // -------------------------------------------------------------------------

    // サーバーを開始（指定ポートでリッスン）
    bool start(int port, int maxClients = 10);

    // サーバーを停止
    void stop();

    // サーバーが稼働中か
    bool isRunning() const;

    // -------------------------------------------------------------------------
    // クライアント管理
    // -------------------------------------------------------------------------

    // 指定クライアントを切断
    void disconnectClient(int clientId);

    // 全クライアントを切断
    void disconnectAllClients();

    // 接続中のクライアント数
    int getClientCount() const;

    // 接続中の全クライアントIDを取得
    std::vector<int> getClientIds() const;

    // クライアント情報を取得（見つからなければnullptr）
    const TcpServerClient* getClient(int clientId) const;

    // -------------------------------------------------------------------------
    // データ送信
    // -------------------------------------------------------------------------

    // 指定クライアントにデータを送信
    bool send(int clientId, const void* data, size_t size);
    bool send(int clientId, const std::vector<char>& data);
    bool send(int clientId, const std::string& message);

    // 全クライアントにブロードキャスト
    void broadcast(const void* data, size_t size);
    void broadcast(const std::vector<char>& data);
    void broadcast(const std::string& message);

    // -------------------------------------------------------------------------
    // 設定
    // -------------------------------------------------------------------------

    // 受信バッファサイズを設定
    void setReceiveBufferSize(size_t size);

    // -------------------------------------------------------------------------
    // 情報取得
    // -------------------------------------------------------------------------

    // リッスン中のポート
    int getPort() const;

private:
    void acceptThreadFunc();
    void clientThreadFunc(int clientId);
    void notifyError(const std::string& msg, int code = 0, int clientId = -1);
    void removeClient(int clientId);

#ifdef _WIN32
    SOCKET serverSocket_ = INVALID_SOCKET;
#else
    int serverSocket_ = -1;
#endif

    int port_ = 0;
    int maxClients_ = 10;

    std::thread acceptThread_;
    std::atomic<bool> running_{false};

    std::unordered_map<int, TcpServerClient> clients_;
    std::unordered_map<int, std::thread> clientThreads_;
    mutable std::mutex clientsMutex_;

    int nextClientId_ = 1;
    size_t receiveBufferSize_ = 65536;

    static std::atomic<int> instanceCount_;
    static void initWinsock();
    static void cleanupWinsock();
};

} // namespace trussc

namespace tc = trussc;
