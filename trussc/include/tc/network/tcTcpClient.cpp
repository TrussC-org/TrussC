// =============================================================================
// tcTcpClient.cpp - TCP クライアントソケット実装
// =============================================================================

#include "tc/network/tcTcpClient.h"
#include "tc/utils/tcLog.h"
#include <cstring>

#ifdef _WIN32
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
    #define WOULD_BLOCK_ERROR WSAEWOULDBLOCK
#else
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
    #define WOULD_BLOCK_ERROR EWOULDBLOCK
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace trussc {

std::atomic<int> TcpClient::instanceCount_{0};

// =============================================================================
// Winsock 初期化（Windows のみ）
// =============================================================================
void TcpClient::initWinsock() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            tcLogError() << "Winsock initialization failed";
        }
        initialized = true;
    }
#endif
}

void TcpClient::cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// =============================================================================
// コンストラクタ / デストラクタ
// =============================================================================
TcpClient::TcpClient() {
    if (instanceCount_++ == 0) {
        initWinsock();
    }
}

TcpClient::~TcpClient() {
    disconnect();
    if (--instanceCount_ == 0) {
        cleanupWinsock();
    }
}

TcpClient::TcpClient(TcpClient&& other) noexcept
    : socket_(other.socket_)
    , remoteHost_(std::move(other.remoteHost_))
    , remotePort_(other.remotePort_)
    , running_(other.running_.load())
    , connected_(other.connected_.load())
    , receiveBufferSize_(other.receiveBufferSize_)
{
#ifdef _WIN32
    other.socket_ = INVALID_SOCKET;
#else
    other.socket_ = -1;
#endif
    other.running_ = false;
    other.connected_ = false;
    instanceCount_++;
}

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        socket_ = other.socket_;
        remoteHost_ = std::move(other.remoteHost_);
        remotePort_ = other.remotePort_;
        running_ = other.running_.load();
        connected_ = other.connected_.load();
        receiveBufferSize_ = other.receiveBufferSize_;

#ifdef _WIN32
        other.socket_ = INVALID_SOCKET;
#else
        other.socket_ = -1;
#endif
        other.running_ = false;
        other.connected_ = false;
    }
    return *this;
}

// =============================================================================
// 接続管理
// =============================================================================
bool TcpClient::connect(const std::string& host, int port) {
    if (connected_) {
        disconnect();
    }

    // ソケット作成
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (socket_ == INVALID_SOCKET) {
#else
    if (socket_ < 0) {
#endif
        notifyError("Failed to create socket", SOCKET_ERROR_CODE);
        return false;
    }

    // ホスト名解決
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

    // 接続
    ret = ::connect(socket_, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (ret == SOCKET_ERROR) {
        notifyError("Failed to connect to " + host + ":" + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(socket_);
#ifdef _WIN32
        socket_ = INVALID_SOCKET;
#else
        socket_ = -1;
#endif
        return false;
    }

    remoteHost_ = host;
    remotePort_ = port;
    connected_ = true;
    running_ = true;

    // 受信スレッド開始
    receiveThread_ = std::thread(&TcpClient::receiveThreadFunc, this);

    tcLogNotice() << "TCP connected to " << host << ":" << port;

    TcpConnectEventArgs args;
    args.success = true;
    args.message = "Connected";
    onConnect.notify(args);

    return true;
}

void TcpClient::connectAsync(const std::string& host, int port) {
    // 既存の接続スレッドがあれば待機
    if (connectThread_.joinable()) {
        connectThread_.join();
    }

    connectThread_ = std::thread(&TcpClient::connectThreadFunc, this, host, port);
}

void TcpClient::connectThreadFunc(const std::string& host, int port) {
    bool success = connect(host, port);
    if (!success) {
        TcpConnectEventArgs args;
        args.success = false;
        args.message = "Connection failed";
        onConnect.notify(args);
    }
}

void TcpClient::disconnect() {
    running_ = false;

#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        CLOSE_SOCKET(socket_);
        socket_ = INVALID_SOCKET;
    }
#else
    if (socket_ >= 0) {
        shutdown(socket_, SHUT_RDWR);
        CLOSE_SOCKET(socket_);
        socket_ = -1;
    }
#endif

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }

    if (connectThread_.joinable()) {
        connectThread_.join();
    }

    if (connected_) {
        connected_ = false;
        TcpDisconnectEventArgs args;
        args.reason = "Disconnected by client";
        args.wasClean = true;
        onDisconnect.notify(args);
    }
}

bool TcpClient::isConnected() const {
    return connected_;
}

// =============================================================================
// データ送受信
// =============================================================================
bool TcpClient::send(const void* data, size_t size) {
    if (!connected_) {
        notifyError("Not connected");
        return false;
    }

    std::lock_guard<std::mutex> lock(sendMutex_);

    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    while (remaining > 0) {
        int sent = static_cast<int>(::send(socket_, ptr, remaining, 0));
        if (sent == SOCKET_ERROR) {
            notifyError("Send failed", SOCKET_ERROR_CODE);
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

bool TcpClient::send(const std::vector<char>& data) {
    return send(data.data(), data.size());
}

bool TcpClient::send(const std::string& message) {
    return send(message.data(), message.size());
}

// =============================================================================
// 受信スレッド
// =============================================================================
void TcpClient::receiveThreadFunc() {
    std::vector<char> buffer(receiveBufferSize_);

    while (running_) {
        int received = static_cast<int>(recv(socket_, buffer.data(), buffer.size(), 0));

        if (received > 0) {
            TcpReceiveEventArgs args;
            args.data.assign(buffer.begin(), buffer.begin() + received);
            onReceive.notify(args);
        } else if (received == 0) {
            // 接続が閉じられた
            running_ = false;
            connected_ = false;
            TcpDisconnectEventArgs args;
            args.reason = "Connection closed by remote";
            args.wasClean = true;
            onDisconnect.notify(args);
            break;
        } else {
            // エラー
            int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK) continue;
#else
            if (err == EWOULDBLOCK || err == EAGAIN) continue;
#endif
            if (running_) {
                running_ = false;
                connected_ = false;
                TcpDisconnectEventArgs args;
                args.reason = "Connection error";
                args.wasClean = false;
                onDisconnect.notify(args);
            }
            break;
        }
    }
}

// =============================================================================
// 設定
// =============================================================================
void TcpClient::setReceiveBufferSize(size_t size) {
    receiveBufferSize_ = size;
}

void TcpClient::setBlocking(bool blocking) {
#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        u_long mode = blocking ? 0 : 1;
        ioctlsocket(socket_, FIONBIO, &mode);
    }
#else
    if (socket_ >= 0) {
        int flags = fcntl(socket_, F_GETFL, 0);
        if (blocking) {
            fcntl(socket_, F_SETFL, flags & ~O_NONBLOCK);
        } else {
            fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
        }
    }
#endif
}

// =============================================================================
// 情報取得
// =============================================================================
std::string TcpClient::getRemoteHost() const {
    return remoteHost_;
}

int TcpClient::getRemotePort() const {
    return remotePort_;
}

// =============================================================================
// エラー通知
// =============================================================================
void TcpClient::notifyError(const std::string& msg, int code) {
    TcpErrorEventArgs args;
    args.message = msg;
    args.errorCode = code;
    onError.notify(args);
}

} // namespace trussc
