#pragma once

#include "tcBaseApp.h"
#include "tcxOsc.h"
#include <tcxImGui.h>
#include <deque>

using namespace tc;
using namespace tcx;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

    void keyPressed(int key) override;

private:
    // OSC sender/receiver
    OscSender sender_;
    OscReceiver receiver_;
    // No EventListener needed for polling style

    // UI state
    char addressBuf_[256] = "/test/message";
    int intValue_ = 42;
    float floatValue_ = 6.28f;
    char stringBuf_[256] = "hello";
    bool sendInt_ = true;
    bool sendFloat_ = true;
    bool sendString_ = false;

    // Logs (separate for send/receive)
    // No mutex needed for polling (only accessed from main thread)
    deque<string> sendLogs_;
    deque<string> receiveLogs_;
    static constexpr size_t MAX_LOG_LINES = 20;

    // Port setting (event uses 9000, polling uses 9001)
    int port_ = 9001;

    // Bundle
    OscBundle pendingBundle_;
    int bundleMessageCount_ = 0;

    // Helpers
    OscMessage createMessage();
    void sendMessage();
    void addToBundle();
    void sendBundle();
    void addSendLog(const string& msg);
    void addReceiveLog(const string& msg);
};
