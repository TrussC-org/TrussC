#pragma once

#include "tcBaseApp.h"
#include "tcxOsc.h"
#include <deque>
#include <mutex>

using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

    void keyPressed(int key) override;

private:
    // OSC 送受信
    OscSender sender_;
    OscReceiver receiver_;
    EventListener messageListener_;
    EventListener errorListener_;

    // UI 状態
    char addressBuf_[256] = "/test/message";
    int intValue_ = 42;
    float floatValue_ = 3.14f;
    char stringBuf_[256] = "hello";
    bool sendInt_ = true;
    bool sendFloat_ = true;
    bool sendString_ = false;

    // ログ（送信・受信別）
    deque<string> sendLogs_;
    deque<string> receiveLogs_;
    mutex sendLogMutex_;
    mutex receiveLogMutex_;
    static constexpr size_t MAX_LOG_LINES = 20;

    // ポート設定
    int port_ = 9000;

    // バンドル用
    OscBundle pendingBundle_;
    int bundleMessageCount_ = 0;

    // ヘルパー
    OscMessage createMessage();
    void sendMessage();
    void addToBundle();
    void sendBundle();
    void addSendLog(const string& msg);
    void addReceiveLog(const string& msg);

    // イベントハンドラ（メンバ関数ポインタ版の例）
    void onParseError(string& error);
};
