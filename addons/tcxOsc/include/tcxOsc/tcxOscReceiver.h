#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include <queue>
#include <mutex>
#include <atomic>

namespace trussc {

// =============================================================================
// OscReceiver - OSC 受信クラス
// =============================================================================
class OscReceiver {
public:
    // イベント
    Event<OscMessage> onMessageReceived;   // メッセージ受信
    Event<OscBundle> onBundleReceived;     // バンドル受信
    Event<std::string> onParseError;       // パースエラー（ロバスト性のため）

    OscReceiver() = default;
    ~OscReceiver() { close(); }

    // コピー禁止
    OscReceiver(const OscReceiver&) = delete;
    OscReceiver& operator=(const OscReceiver&) = delete;

    // -------------------------------------------------------------------------
    // 設定
    // -------------------------------------------------------------------------

    // 受信を開始
    bool setup(int port) {
        port_ = port;

        // 受信イベントを設定
        receiveListener_ = socket_.onReceive.listen([this](UdpReceiveEventArgs& args) {
            handleReceive(args);
        });

        errorListener_ = socket_.onError.listen([this](UdpErrorEventArgs& args) {
            std::string msg = "Socket error: " + args.message;
            onParseError.notify(msg);
        });

        return socket_.bind(port, true);  // 受信スレッド自動開始
    }

    // 閉じる
    void close() {
        socket_.close();
        receiveListener_.disconnect();
        errorListener_.disconnect();
        port_ = 0;
    }

    // -------------------------------------------------------------------------
    // 情報
    // -------------------------------------------------------------------------

    int getPort() const { return port_; }
    bool isListening() const { return socket_.isReceiving(); }

    // -------------------------------------------------------------------------
    // ポーリング API（初回呼び出しでバッファ有効化）
    // -------------------------------------------------------------------------

    // 未読メッセージがあるか（初回呼び出しでバッファ有効化）
    bool hasNewMessage() {
        bufferEnabled_ = true;
        std::lock_guard<std::mutex> lock(queueMutex_);
        return !messageQueue_.empty();
    }

    // 次のメッセージを取得（キューから削除）
    bool getNextMessage(OscMessage& msg) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (messageQueue_.empty()) return false;
        msg = std::move(messageQueue_.front());
        messageQueue_.pop();
        return true;
    }

    // バッファサイズを設定（デフォルト100件）
    void setBufferSize(size_t size) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        bufferMax_ = size;
        // 現在のキューが上限超えてたら削る
        while (messageQueue_.size() > bufferMax_) {
            messageQueue_.pop();
        }
    }

    size_t getBufferSize() const { return bufferMax_; }

private:
    void handleReceive(UdpReceiveEventArgs& args) {
        if (args.data.empty()) return;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(args.data.data());
        size_t size = args.data.size();

        parsePacket(data, size);
    }

    void parsePacket(const uint8_t* data, size_t size) {
        if (size < 4) {
            std::string err = "Packet too small";
            onParseError.notify(err);
            return;
        }

        // バンドルかメッセージか判定
        if (OscBundle::isBundle(data, size)) {
            bool ok = false;
            OscBundle bundle = OscBundle::fromBytes(data, size, ok);
            if (ok) {
                // バンドル内のメッセージも個別に通知
                dispatchBundle(bundle);
            }
            else {
                std::string err = "Failed to parse bundle";
                onParseError.notify(err);
            }
        }
        else {
            bool ok = false;
            OscMessage msg = OscMessage::fromBytes(data, size, ok);
            if (ok) {
                // バッファ有効ならキューに追加
                if (bufferEnabled_) {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    messageQueue_.push(msg);
                    while (messageQueue_.size() > bufferMax_) {
                        messageQueue_.pop();
                    }
                }
                // リスナーには常に通知
                onMessageReceived.notify(msg);
            }
            else {
                std::string err = "Failed to parse message";
                onParseError.notify(err);
            }
        }
    }

    // バンドル内のメッセージを再帰的に通知
    void dispatchBundle(OscBundle bundle) {
        onBundleReceived.notify(bundle);

        for (size_t i = 0; i < bundle.getElementCount(); ++i) {
            if (bundle.isMessage(i)) {
                OscMessage msg = bundle.getMessageAt(i);
                // バッファ有効ならキューに追加
                if (bufferEnabled_) {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    messageQueue_.push(msg);
                    while (messageQueue_.size() > bufferMax_) {
                        messageQueue_.pop();
                    }
                }
                onMessageReceived.notify(msg);
            }
            else if (bundle.isBundle(i)) {
                dispatchBundle(bundle.getBundleAt(i));
            }
        }
    }

    UdpSocket socket_;
    int port_ = 0;
    EventListener receiveListener_;
    EventListener errorListener_;

    // ポーリング用バッファ
    std::queue<OscMessage> messageQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> bufferEnabled_{false};
    size_t bufferMax_ = 100;
};

}  // namespace trussc
