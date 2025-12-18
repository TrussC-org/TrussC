#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include "tc/comm/tcSerial.h"

// =============================================================================
// tcApp - シリアル通信サンプル
// openFrameworks の serialExample と同等の機能を持つ
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // シリアル通信
    Serial serial;

    // デバイスリスト
    std::vector<SerialDeviceInfo> deviceList;

    // メッセージ送受信
    bool bSendSerialMessage = false;
    std::string messageToSend;
    std::vector<std::string> receivedMessages;
    std::string serialReadBuffer;

    // 接続リトライ用
    float timeLastTryConnect = 0.0f;

    // 最後に読み取った時刻（ハイライト表示用）
    float readTime = 0.0f;

    // 接続先デバイスパス（空なら最初のデバイス）
    std::string serialDevicePath = "";
};
