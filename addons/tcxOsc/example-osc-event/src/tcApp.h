#pragma once

#include "tcBaseApp.h"
#include "tcxOsc.h"
#include <tcxImGui.h>
#include <deque>
#include <mutex>

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
    EventListener messageListener_;
    EventListener errorListener_;

    // UI state
    char addressBuf_[256] = "/test/message";
    int intValue_ = 42;
    float floatValue_ = 6.28f;
    char stringBuf_[256] = "hello";
    bool sendInt_ = true;
    bool sendFloat_ = true;
    bool sendString_ = false;

    // Logs (separate for send/receive)
    deque<string> sendLogs_;
    deque<string> receiveLogs_;
    mutex sendLogMutex_;
    mutex receiveLogMutex_;
    static constexpr size_t MAX_LOG_LINES = 20;

    // Port setting
    int port_ = 9000;

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

    // Event handler (member function pointer example)
    void onParseError(string& error);
};
