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
    UdpSocket receiver;
    UdpSocket sender;

    EventListener receiveListener;
    EventListener errorListener;

    std::vector<std::string> receivedMessages;
    std::mutex messagesMutex;

    int sendCount = 0;
};
