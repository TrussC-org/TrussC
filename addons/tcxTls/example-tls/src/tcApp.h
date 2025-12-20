#pragma once

#include "tcBaseApp.h"
#include "tcTlsClient.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void cleanup() override;

private:
    TlsClient client;

    // イベントリスナー
    EventListener connectListener;
    EventListener receiveListener;
    EventListener disconnectListener;
    EventListener errorListener;

    // 送信ログ（左側）
    std::vector<std::string> sentMessages;
    std::mutex sentMutex;

    // 受信ログ（右側）
    std::vector<std::string> receivedMessages;
    std::mutex receivedMutex;

    // 状態
    bool isConnected = false;
    std::string statusMessage = "Press C to connect to httpbin.org";

    void addSent(const std::string& msg);
    void addReceived(const std::string& msg);
    void sendHttpRequest();
};
