#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void cleanup() override;

private:
    // Server mode
    TcpServer server;

    // Client mode
    TcpClient client;

    // Event listeners
    EventListener clientConnectListener;
    EventListener clientDisconnectListener;
    EventListener serverReceiveListener;
    EventListener serverErrorListener;

    EventListener clientConnectedListener;
    EventListener clientReceiveListener;
    EventListener clientDisconnectListener2;
    EventListener clientErrorListener;

    // Log messages
    std::vector<std::string> logMessages;
    std::mutex logMutex;

    // State
    int messageCount = 0;

    void addLog(const std::string& msg);
};
