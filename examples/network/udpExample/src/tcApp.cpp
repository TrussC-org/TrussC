// =============================================================================
// tcApp.cpp - UDP ソケットサンプル
// =============================================================================

#include "tcApp.h"
#include <sstream>

using namespace std;
using namespace trussc;

void tcApp::setup() {
    tcLogNotice() << "=== UDP Socket Example ===";
    tcLogNotice() << "Press SPACE to send a message";
    tcLogNotice() << "Press C to clear messages";
    tcLogNotice() << "==========================";

    // 受信イベントをリッスン
    receiveListener = receiver.onReceive.listen([this](UdpReceiveEventArgs& e) {
        string msg(e.data.begin(), e.data.end());
        tcLogNotice() << "Received from " << e.remoteHost << ":" << e.remotePort << " -> " << msg;

        lock_guard<mutex> lock(messagesMutex);
        receivedMessages.push_back(e.remoteHost + ":" + to_string(e.remotePort) + " -> " + msg);
        if (receivedMessages.size() > 20) {
            receivedMessages.erase(receivedMessages.begin());
        }
    });

    // エラーイベントをリッスン
    errorListener = receiver.onError.listen([](UdpErrorEventArgs& e) {
        tcLogError() << "UDP Error: " << e.message;
    });

    // 受信用ソケットをバインド（ポート9000で受信開始）
    if (!receiver.bind(9000)) {
        tcLogError() << "Failed to bind receiver to port 9000";
    }

    // 送信用ソケットの送信先を設定
    sender.connect("127.0.0.1", 9000);
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(30);

    float y = 40;

    // タイトル
    setColor(255);
    drawBitmapString("UDP Socket Example", 40, y);
    y += 30;

    // 操作説明
    setColor(180);
    drawBitmapString("SPACE: Send message   C: Clear", 40, y);
    y += 40;

    // 送信カウント
    setColor(100, 200, 255);
    drawBitmapString("Sent: " + to_string(sendCount) + " messages", 40, y);
    y += 30;

    // 受信メッセージ
    setColor(100, 255, 100);
    drawBitmapString("Received Messages:", 40, y);
    y += 25;

    setColor(220);
    lock_guard<mutex> lock(messagesMutex);
    for (const auto& msg : receivedMessages) {
        drawBitmapString(msg, 50, y);
        y += 18;
    }
}

void tcApp::keyPressed(int key) {
    if (key == KEY_SPACE || key == ' ') {
        // メッセージを送信
        sendCount++;
        ostringstream oss;
        oss << "Hello from TrussC! #" << sendCount;
        string msg = oss.str();

        if (sender.send(msg)) {
            tcLogNotice() << "Sent: " << msg;
        }
    } else if (key == 'C' || key == 'c') {
        lock_guard<mutex> lock(messagesMutex);
        receivedMessages.clear();
        sendCount = 0;
        tcLogNotice() << "Messages cleared";
    }
}

void tcApp::cleanup() {
    receiver.close();
    sender.close();
}
