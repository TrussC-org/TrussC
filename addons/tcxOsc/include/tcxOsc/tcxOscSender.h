#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"

namespace trussc {

// =============================================================================
// OscSender - OSC 送信クラス
// =============================================================================
class OscSender {
public:
    OscSender() = default;
    ~OscSender() { close(); }

    // コピー禁止
    OscSender(const OscSender&) = delete;
    OscSender& operator=(const OscSender&) = delete;

    // -------------------------------------------------------------------------
    // 設定
    // -------------------------------------------------------------------------

    // 送信先を設定
    bool setup(const std::string& host, int port) {
        host_ = host;
        port_ = port;
        return socket_.connect(host, port);
    }

    // 閉じる
    void close() {
        socket_.close();
        host_.clear();
        port_ = 0;
    }

    // -------------------------------------------------------------------------
    // 送信
    // -------------------------------------------------------------------------

    // メッセージを送信
    bool send(const OscMessage& msg) {
        auto bytes = msg.toBytes();
        return socket_.send(bytes.data(), bytes.size());
    }

    // バンドルを送信
    bool send(const OscBundle& bundle) {
        auto bytes = bundle.toBytes();
        return socket_.send(bytes.data(), bytes.size());
    }

    // -------------------------------------------------------------------------
    // 情報
    // -------------------------------------------------------------------------

    const std::string& getHost() const { return host_; }
    int getPort() const { return port_; }
    bool isConnected() const { return socket_.isValid(); }

private:
    UdpSocket socket_;
    std::string host_;
    int port_ = 0;
};

}  // namespace trussc
