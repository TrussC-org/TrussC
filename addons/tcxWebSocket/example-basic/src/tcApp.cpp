#include "tcApp.h"

using namespace std;
using namespace tc;

void tcApp::setup() {
    logNotice() << "WebSocket setup";

    ws_.onOpen.listen(onOpen_, [this]() {
        lock_guard<mutex> lock(messageMutex_);
        pendingMessages_.push_back("Connected!");
        ws_.send("Hello from TrussC!");
    });

    ws_.onMessage.listen(onMessage_, [this](WebSocketEventArgs& e) {
        lock_guard<mutex> lock(messageMutex_);
        pendingMessages_.push_back(">> " + e.message);
    });

    ws_.onClose.listen(onClose_, [this]() {
        lock_guard<mutex> lock(messageMutex_);
        pendingMessages_.push_back("Disconnected");
    });

    ws_.onError.listen(onError_, [this](TcpErrorEventArgs& e) {
        lock_guard<mutex> lock(messageMutex_);
        pendingMessages_.push_back("Error: " + e.message);
    });

    // Connect to a public echo server
    // For Wasm, we must use wss:// because the page will likely be HTTPS or localhost
    // And non-secure ws:// is often blocked by browsers if origin is secure.
    // Also, direct TCP sockets are not available in Wasm, so TcpClient will likely fail
    // unless compiled with Emscripten's WebSocket/TCP emulation or if we use WebSocket directly.
    // But tcxWebSocket wraps TcpClient, so it should work if TcpClient works in Wasm (via emulation).
    ws_.connect("wss://echo.websocket.org");
}

void tcApp::update() {
    // Move pending messages to main list
    {
        lock_guard<mutex> lock(messageMutex_);
        if (!pendingMessages_.empty()) {
            for (auto& msg : pendingMessages_) {
                messages_.push_back(msg);
            }
            pendingMessages_.clear();
        }
    }

    // Keep list size manageable
    while (messages_.size() > 25) {
        messages_.erase(messages_.begin());
    }
}

void tcApp::draw() {
    clear(0.1f);
    
    setColor(1.0f);
    drawBitmapString("WebSocket Example", 20, 30);
    drawBitmapString("Press 's' to send message", 20, 50);
    drawBitmapString("Status: " + string(ws_.isConnected() ? "Connected" : "Disconnected"), 20, 70);

    for (size_t i = 0; i < messages_.size(); ++i) {
        drawBitmapString(messages_[i], 20, 100 + i * 20);
    }
}

void tcApp::keyPressed(int key) {
    // Sokol keycode for 'S' is 83. ASCII 's' is 115.
    if (key == 's' || key == 'S' || key == 83) {
        if (ws_.isConnected()) {
            string msg = "Test message " + to_string(getFrameCount());
            ws_.send(msg);
            messages_.push_back("Sent: " + msg);
        }
    }
}