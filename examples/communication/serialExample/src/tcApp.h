#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include "tc/comm/tcSerial.h"

// =============================================================================
// tcApp - Serial Communication Sample
// Provides equivalent functionality to openFrameworks' serialExample
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Serial communication
    Serial serial;

    // Device list
    std::vector<SerialDeviceInfo> deviceList;

    // Message send/receive
    bool bSendSerialMessage = false;
    std::string messageToSend;
    std::vector<std::string> receivedMessages;
    std::string serialReadBuffer;

    // For connection retry
    float timeLastTryConnect = 0.0f;

    // Last read time (for highlight display)
    float readTime = 0.0f;

    // Target device path (if empty, use first device)
    std::string serialDevicePath = "";
};
