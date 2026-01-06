#pragma once

#include <TrussC.h>
#include "tcWebSocketClient.h"

using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    WebSocketClient ws_;
    std::vector<std::string> messages_;
    std::vector<std::string> pendingMessages_;
    std::mutex messageMutex_;
    
    EventListener onOpen_;
    EventListener onMessage_;
    EventListener onClose_;
    EventListener onError_;
};
