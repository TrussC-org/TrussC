#include "tcApp.h"
#include "TrussC.h"

// =============================================================================
// setup - 初期化
// =============================================================================
void tcApp::setup() {
    tc::setVsync(true);

    // デバイス一覧を取得
    serial.listDevices();
    deviceList = serial.getDeviceList();

    // 接続先デバイスパスを設定（例: Arduino）
    // serialDevicePath = "/dev/tty.usbserial-A10172HG";

    int baud = 9600;

    // 指定されたデバイスがあればそこに、なければ最初のデバイスに接続
    if (!serialDevicePath.empty()) {
        serial.setup(serialDevicePath, baud);
    } else if (!deviceList.empty()) {
        serial.setup(0, baud);
    }

    timeLastTryConnect = tc::getElapsedTime();
    readTime = 0;
}

// =============================================================================
// update - 更新処理
// =============================================================================
void tcApp::update() {
    if (serial.isInitialized()) {
        // メッセージ送信
        if (bSendSerialMessage && !messageToSend.empty()) {
            serial.writeBytes(messageToSend);
            messageToSend.clear();
            bSendSerialMessage = false;
        }

        // データ受信
        int numBytesToRead = serial.available();
        if (numBytesToRead > 512) {
            numBytesToRead = 512;
        }

        if (numBytesToRead > 0) {
            std::string buffer;
            serial.readBytes(buffer, numBytesToRead);

            // 受信データを処理（改行で分割、なければバッファに追加）
            for (char c : buffer) {
                if (c == '\n' || c == '\r') {
                    if (!serialReadBuffer.empty()) {
                        receivedMessages.push_back(serialReadBuffer);
                        serialReadBuffer.clear();
                    }
                } else {
                    serialReadBuffer += c;
                }
            }

            // 改行がなくてもデータがあれば表示（Arduinoが改行なしで送る場合用）
            if (!serialReadBuffer.empty() && serialReadBuffer.length() >= 3) {
                receivedMessages.push_back(serialReadBuffer);
                serialReadBuffer.clear();
            }

            readTime = tc::getElapsedTime();
        }
    } else {
        // 未接続の場合、10秒ごとに再接続を試行
        float now = tc::getElapsedTime();
        if (now - timeLastTryConnect > 10.0f) {
            deviceList = serial.getDeviceList();
            timeLastTryConnect = now;

            int baud = 9600;
            if (!serialDevicePath.empty()) {
                printf("Attempting to connect to serial device: %s\n", serialDevicePath.c_str());
                serial.setup(serialDevicePath, baud);
            } else if (!deviceList.empty()) {
                printf("Attempting to connect to serial device: 0\n");
                serial.setup(0, baud);
            }
        }
    }

    // 古いメッセージを削除（最大10件）
    while (receivedMessages.size() > 10) {
        receivedMessages.erase(receivedMessages.begin());
    }
}

// =============================================================================
// draw - 描画
// =============================================================================
void tcApp::draw() {
    tc::clear(255);
    tc::setColor(40);

    // 接続状態
    std::string connStr = "Serial connected: ";
    connStr += serial.isInitialized() ? "true" : "false";
    if (serial.isInitialized()) {
        connStr += " (" + serial.getDevicePath() + ")";
    }
    tc::drawBitmapString(connStr, 50, 40);

    // デバイス一覧
    std::string deviceStr = "Devices:\n";
    for (const auto& dev : deviceList) {
        deviceStr += std::to_string(dev.getDeviceID()) + ": " + dev.getDevicePath() + "\n";
    }
    tc::drawBitmapString(deviceStr, 50, 60);

    // 送信メッセージ
    std::string msgStr = "Type to send message\n";
    if (!messageToSend.empty()) {
        msgStr += messageToSend;
    }
    tc::drawBitmapString(msgStr, 50, 400, 2.0f);

    // 受信メッセージ
    float posY = 60;
    tc::drawBitmapString("Received messages", 550, posY, 2.0f);
    posY += 42;

    for (int i = (int)receivedMessages.size() - 1; i >= 0; i--) {
        // 最新のメッセージをハイライト
        if (i == (int)receivedMessages.size() - 1 && (tc::getElapsedTime() - readTime) < 0.5f) {
            tc::setColor(40);
        } else {
            tc::setColor(120);
        }
        tc::drawBitmapString(receivedMessages[i], 550, posY, 2.0f);
        posY += 42;
    }
}

// =============================================================================
// keyPressed - キー入力
// =============================================================================
void tcApp::keyPressed(int key) {
    // Enter: メッセージ送信
    if (key == tc::KEY_ENTER) {
        if (!messageToSend.empty()) {
            bSendSerialMessage = true;
        }
    }
    // Backspace/Delete: 1文字削除
    else if (key == tc::KEY_BACKSPACE || key == tc::KEY_DELETE || key == 27) {
        if (!messageToSend.empty()) {
            messageToSend.pop_back();
        }
    }
    // Escape: メッセージクリア
    else if (key == tc::KEY_ESCAPE) {
        messageToSend.clear();
    }
    // 通常の文字（sokol_appのキーコードは大文字ASCIIと同じなので小文字に変換）
    else if (key >= 32 && key < 127) {
        char c = static_cast<char>(key);
        // A-Z (65-90) を小文字に変換
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;  // 'a' - 'A' = 32
        }
        messageToSend += c;
    }
}
