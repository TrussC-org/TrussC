// =============================================================================
// tcApp.cpp - TCP ソケットサンプル（サーバー＆クライアント）
// =============================================================================

#include "tcApp.h"
#include <sstream>

using namespace std;
using namespace trussc;

void tcApp::setup() {
    tcLogNotice("tcApp") << "=== TCP Socket Example ===";
    tcLogNotice("tcApp") << "Press S to start Server (port 9001)";
    tcLogNotice("tcApp") << "Press C to connect as Client";
    tcLogNotice("tcApp") << "Press SPACE to send a message";
    tcLogNotice("tcApp") << "Press D to disconnect";
    tcLogNotice("tcApp") << "Press X to clear log";
    tcLogNotice("tcApp") << "==========================";

    addLog("Press S for Server, C for Client");

    // サーバーイベント設定
    clientConnectListener = server.onClientConnect.listen([this](TcpClientConnectEventArgs& e) {
        ostringstream oss;
        oss << "[Server] Client " << e.clientId << " connected from " << e.host << ":" << e.port;
        addLog(oss.str());
    });

    clientDisconnectListener = server.onClientDisconnect.listen([this](TcpClientDisconnectEventArgs& e) {
        ostringstream oss;
        oss << "[Server] Client " << e.clientId << " disconnected: " << e.reason;
        addLog(oss.str());
    });

    serverReceiveListener = server.onReceive.listen([this](TcpServerReceiveEventArgs& e) {
        string msg(e.data.begin(), e.data.end());
        ostringstream oss;
        oss << "[Server] Received from client " << e.clientId << ": " << msg;
        addLog(oss.str());

        // エコーバック
        string reply = "Echo: " + msg;
        server.send(e.clientId, reply);
    });

    serverErrorListener = server.onError.listen([this](TcpServerErrorEventArgs& e) {
        ostringstream oss;
        oss << "[Server] Error: " << e.message;
        addLog(oss.str());
    });

    // クライアントイベント設定
    clientConnectedListener = client.onConnect.listen([this](TcpConnectEventArgs& e) {
        ostringstream oss;
        if (e.success) {
            oss << "[Client] Connected to server";
        } else {
            oss << "[Client] Connection failed: " << e.message;
        }
        addLog(oss.str());
    });

    clientReceiveListener = client.onReceive.listen([this](TcpReceiveEventArgs& e) {
        string msg(e.data.begin(), e.data.end());
        addLog("[Client] Received: " + msg);
    });

    clientDisconnectListener2 = client.onDisconnect.listen([this](TcpDisconnectEventArgs& e) {
        addLog("[Client] Disconnected: " + e.reason);
    });

    clientErrorListener = client.onError.listen([this](TcpErrorEventArgs& e) {
        addLog("[Client] Error: " + e.message);
    });
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(30);

    float y = 40;

    // タイトル
    setColor(1.0f);
    drawBitmapString("TCP Socket Example", 40, y);
    y += 30;

    // 状態表示
    setColor(0.4f, 0.78f, 1.0f);
    ostringstream status;
    if (server.isRunning()) {
        status << "Server running on port " << server.getPort();
        status << " (Clients: " << server.getClientCount() << ")";
    } else {
        status << "Server not running";
    }
    drawBitmapString(status.str(), 40, y);
    y += 20;

    setColor(0.4f, 1.0f, 0.4f);
    if (client.isConnected()) {
        drawBitmapString("Client connected to " + client.getRemoteHost(), 40, y);
    } else {
        drawBitmapString("Client not connected", 40, y);
    }
    y += 30;

    // 操作説明
    setColor(0.7f);
    drawBitmapString("S: Start Server  C: Connect Client  SPACE: Send  D: Disconnect  X: Clear", 40, y);
    y += 30;

    // ログ表示
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString("Log:", 40, y);
    y += 25;

    setColor(0.86f);
    lock_guard<mutex> lock(logMutex);
    for (const auto& msg : logMessages) {
        drawBitmapString(msg, 50, y);
        y += 18;
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'S' || key == 's') {
        // サーバー開始
        if (!server.isRunning()) {
            if (server.start(9001)) {
                addLog("[Server] Started on port 9001");
            }
        } else {
            addLog("[Server] Already running");
        }
    } else if (key == 'C' || key == 'c') {
        // クライアント接続
        if (!client.isConnected()) {
            addLog("[Client] Connecting to 127.0.0.1:9001...");
            if (client.connect("127.0.0.1", 9001)) {
                addLog("[Client] Connected!");
            } else {
                addLog("[Client] Connection failed");
            }
        } else {
            addLog("[Client] Already connected");
        }
    } else if (key == KEY_SPACE || key == ' ') {
        messageCount++;

        // サーバーからブロードキャスト
        if (server.isRunning() && server.getClientCount() > 0) {
            ostringstream oss;
            oss << "Server broadcast #" << messageCount;
            server.broadcast(oss.str());
            addLog("[Server] Broadcast: " + oss.str());
        }

        // クライアントから送信
        if (client.isConnected()) {
            ostringstream oss;
            oss << "Hello from client #" << messageCount;
            client.send(oss.str());
            addLog("[Client] Sent: " + oss.str());
        }
    } else if (key == 'D' || key == 'd') {
        // 切断
        if (client.isConnected()) {
            client.disconnect();
            addLog("[Client] Disconnecting...");
        }
        if (server.isRunning()) {
            server.stop();
            addLog("[Server] Stopped");
        }
    } else if (key == 'X' || key == 'x') {
        {
            lock_guard<mutex> lock(logMutex);
            logMessages.clear();
            messageCount = 0;
        }
        addLog("Log cleared");
    }
}

void tcApp::cleanup() {
    client.disconnect();
    server.stop();
}

void tcApp::addLog(const string& msg) {
    tcLogNotice("tcApp") << msg;

    lock_guard<mutex> lock(logMutex);
    logMessages.push_back(msg);
    if (logMessages.size() > 20) {
        logMessages.erase(logMessages.begin());
    }
}
