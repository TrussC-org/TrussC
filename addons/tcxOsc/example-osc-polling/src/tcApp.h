#pragma once

#include "tcBaseApp.h"
#include "tcxOsc.h"
#include <deque>

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
    // ポーリング形式なので EventListener は不要

    // UI 状態
    char addressBuf_[256] = "/test/message";
    int intValue_ = 42;
    float floatValue_ = 3.14f;
    char stringBuf_[256] = "hello";
    bool sendInt_ = true;
    bool sendFloat_ = true;
    bool sendString_ = false;

    // ログ（送信・受信別）
    // ポーリング形式なので mutex は不要（メインスレッドでのみアクセス）
    deque<string> sendLogs_;
    deque<string> receiveLogs_;
    static constexpr size_t MAX_LOG_LINES = 20;

    // ポート設定（event版は9000、polling版は9001）
    int port_ = 9001;

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
};
