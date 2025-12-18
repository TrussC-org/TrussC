#pragma once

#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void cleanup() override;

private:
    // サーバーモードで起動
    TcpServer server;

    // クライアントモード用
    TcpClient client;

    // イベントリスナー
    EventListener clientConnectListener;
    EventListener clientDisconnectListener;
    EventListener serverReceiveListener;
    EventListener serverErrorListener;

    EventListener clientConnectedListener;
    EventListener clientReceiveListener;
    EventListener clientDisconnectListener2;
    EventListener clientErrorListener;

    // ログメッセージ
    std::vector<std::string> logMessages;
    std::mutex logMutex;

    // 状態
    bool isServerMode = true;
    int messageCount = 0;

    void addLog(const std::string& msg);
};
