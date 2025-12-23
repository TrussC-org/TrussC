// =============================================================================
// tcApp.cpp - TLS (HTTPS) クライアントサンプル
// =============================================================================

#include "tcApp.h"
#include <sstream>

using namespace std;

void tcApp::setup() {
    tcLogNotice() << "=== TLS (HTTPS) Client Example ===";
    tcLogNotice() << "Press C to connect to httpbin.org (HTTPS)";
    tcLogNotice() << "Press SPACE to send HTTP GET request";
    tcLogNotice() << "Press D to disconnect";
    tcLogNotice() << "Press X to clear log";
    tcLogNotice() << "==================================";

    // TLS 設定（証明書検証なし - テスト用）
    client.setVerifyNone();

    // 接続イベント
    client.onConnect.listen(connectListener, [this](TcpConnectEventArgs& e) {
        if (e.success) {
            isConnected = true;
            statusMessage = "Connected! TLS: " + client.getTlsVersion() + " / " + client.getCipherSuite();
            addReceived("--- Connected ---");
            addReceived("TLS Version: " + client.getTlsVersion());
            addReceived("Cipher: " + client.getCipherSuite());
        } else {
            statusMessage = "Connection failed: " + e.message;
            addReceived("--- Connection failed ---");
        }
    });

    // 受信イベント
    client.onReceive.listen(receiveListener, [this](TcpReceiveEventArgs& e) {
        string data(e.data.begin(), e.data.end());

        // 長すぎる場合は分割
        istringstream iss(data);
        string line;
        while (getline(iss, line)) {
            if (line.length() > 80) {
                line = line.substr(0, 77) + "...";
            }
            addReceived(line);
        }
    });

    // 切断イベント
    client.onDisconnect.listen(disconnectListener, [this](TcpDisconnectEventArgs& e) {
        isConnected = false;
        statusMessage = "Disconnected: " + e.reason;
        addReceived("--- Disconnected ---");
    });

    // エラーイベント
    client.onError.listen(errorListener, [this](TcpErrorEventArgs& e) {
        addReceived("ERROR: " + e.message);
    });
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);

    float w = getWindowWidth();
    float h = getWindowHeight();
    float midX = w / 2;

    // タイトル
    setColor(1.0f);
    drawBitmapString("TLS (HTTPS) Client Example", 40, 30);

    // 状態表示
    if (isConnected) {
        setColor(0.4f, 1.0f, 0.4f);
    } else {
        setColor(1.0f, 0.4f, 0.4f);
    }
    drawBitmapString(statusMessage, 40, 55);

    // 操作説明
    setColor(0.7f);
    drawBitmapString("C: Connect  SPACE: Send Request  D: Disconnect  X: Clear", 40, 80);

    // 中央の区切り線
    setColor(0.3f);
    drawLine(midX, 100, midX, h - 20);

    // 左側: 送信ログ
    setColor(0.4f, 0.8f, 1.0f);
    drawBitmapString("SENT", 40, 110);
    setColor(0.24f);
    drawRect(30, 125, midX - 50, h - 150);

    setColor(0.8f, 0.86f, 1.0f);
    float y = 140;
    {
        lock_guard<mutex> lock(sentMutex);
        for (const auto& msg : sentMessages) {
            drawBitmapString(msg, 40, y);
            y += 16;
            if (y > h - 40) break;
        }
    }

    // 右側: 受信ログ
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString("RECEIVED", midX + 20, 110);
    setColor(0.24f);
    drawRect(midX + 10, 125, midX - 50, h - 150);

    setColor(0.8f, 1.0f, 0.8f);
    y = 140;
    {
        lock_guard<mutex> lock(receivedMutex);
        for (const auto& msg : receivedMessages) {
            drawBitmapString(msg, midX + 20, y);
            y += 16;
            if (y > h - 40) break;
        }
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'C' || key == 'c') {
        if (!isConnected) {
            statusMessage = "Connecting to httpbin.org:443...";
            addSent("Connecting to httpbin.org:443...");

            // HTTPS 接続
            if (client.connect("httpbin.org", 443)) {
                // 接続成功は onConnect で通知される
            }
        } else {
            statusMessage = "Already connected";
        }
    } else if (key == KEY_SPACE || key == ' ') {
        if (isConnected) {
            sendHttpRequest();
        } else {
            statusMessage = "Not connected. Press C first.";
        }
    } else if (key == 'D' || key == 'd') {
        if (isConnected) {
            client.disconnect();
        }
    } else if (key == 'X' || key == 'x') {
        {
            lock_guard<mutex> lock(sentMutex);
            sentMessages.clear();
        }
        {
            lock_guard<mutex> lock(receivedMutex);
            receivedMessages.clear();
        }
        statusMessage = "Log cleared";
    }
}

void tcApp::sendHttpRequest() {
    // HTTP GET リクエストを送信
    string request =
        "GET /get HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "User-Agent: TrussC/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";

    addSent("--- HTTP Request ---");
    addSent("GET /get HTTP/1.1");
    addSent("Host: httpbin.org");
    addSent("User-Agent: TrussC/1.0");
    addSent("Accept: */*");
    addSent("Connection: close");

    if (client.send(request)) {
        addSent("--- Sent successfully ---");
    } else {
        addSent("--- Send failed ---");
    }
}

void tcApp::cleanup() {
    client.disconnect();
}

void tcApp::addSent(const string& msg) {
    tcLogNotice() << "[SENT] " << msg;
    lock_guard<mutex> lock(sentMutex);
    sentMessages.push_back(msg);
    if (sentMessages.size() > 30) {
        sentMessages.erase(sentMessages.begin());
    }
}

void tcApp::addReceived(const string& msg) {
    tcLogNotice() << "[RECV] " << msg;
    lock_guard<mutex> lock(receivedMutex);
    receivedMessages.push_back(msg);
    if (receivedMessages.size() > 30) {
        receivedMessages.erase(receivedMessages.begin());
    }
}
