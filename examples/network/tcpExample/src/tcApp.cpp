// =============================================================================
// tcApp.cpp - TCP Socket Sample (Server & Client)
// =============================================================================

#include "tcApp.h"
#include <sstream>

using namespace std;
using namespace trussc;

void tcApp::setup() {
    logNotice("tcApp") << "=== TCP Socket Example ===";
    logNotice("tcApp") << "Press S to start Server (port 9001)";
    logNotice("tcApp") << "Press C to connect as Client";
    logNotice("tcApp") << "Press SPACE to send a message";
    logNotice("tcApp") << "Press D to disconnect";
    logNotice("tcApp") << "Press X to clear log";
    logNotice("tcApp") << "==========================";

    addLog("Press S for Server, C for Client");

    // Network events fire on internal threads; Deliver::Main marshals each
    // listener onto the main thread, so app state can be touched without locks.

    // Server event setup
    clientConnectListener = server.onClientConnect.listen([this](TcpClientConnectEventArgs& e) {
        ostringstream oss;
        oss << "[Server] Client " << e.clientId << " connected from " << e.host << ":" << e.port;
        addLog(oss.str());
    }, Deliver::Main);

    clientDisconnectListener = server.onClientDisconnect.listen([this](TcpClientDisconnectEventArgs& e) {
        ostringstream oss;
        oss << "[Server] Client " << e.clientId << " disconnected: " << e.reason;
        addLog(oss.str());
    }, Deliver::Main);

    serverReceiveListener = server.onReceive.listen([this](TcpServerReceiveEventArgs& e) {
        string msg(e.data.begin(), e.data.end());
        ostringstream oss;
        oss << "[Server] Received from client " << e.clientId << ": " << msg;
        addLog(oss.str());

        // Echo back
        string reply = "Echo: " + msg;
        server.send(e.clientId, reply);
    }, Deliver::Main);

    serverErrorListener = server.onError.listen([this](TcpServerErrorEventArgs& e) {
        ostringstream oss;
        oss << "[Server] Error: " << e.message;
        addLog(oss.str());
    }, Deliver::Main);

    // Client event setup
    clientConnectedListener = client.onConnect.listen([this](TcpConnectEventArgs& e) {
        ostringstream oss;
        if (e.success) {
            oss << "[Client] Connected to server";
        } else {
            oss << "[Client] Connection failed: " << e.message;
        }
        addLog(oss.str());
    }, Deliver::Main);

    clientReceiveListener = client.onReceive.listen([this](TcpReceiveEventArgs& e) {
        string msg(e.data.begin(), e.data.end());
        addLog("[Client] Received: " + msg);
    }, Deliver::Main);

    clientDisconnectListener2 = client.onDisconnect.listen([this](TcpDisconnectEventArgs& e) {
        addLog("[Client] Disconnected: " + e.reason);
    }, Deliver::Main);

    clientErrorListener = client.onError.listen([this](TcpErrorEventArgs& e) {
        addLog("[Client] Error: " + e.message);
    }, Deliver::Main);
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;

    // Title
    setColor(1.0f);
    drawBitmapString("TCP Socket Example", 40, y);
    y += 30;

    // Status display
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

    // Instructions
    setColor(0.7f);
    drawBitmapString("S: Start Server  C: Connect Client  SPACE: Send  D: Disconnect  X: Clear", 40, y);
    y += 30;

    // Log display
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString("Log:", 40, y);
    y += 25;

    setColor(0.86f);
    for (const auto& msg : logMessages) {
        drawBitmapString(msg, 50, y);
        y += 18;
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'S') {
        // Start server
        if (!server.isRunning()) {
            if (server.start(9001)) {
                addLog("[Server] Started on port 9001");
            }
        } else {
            addLog("[Server] Already running");
        }
    } else if (key == 'C') {
        // Client connect
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

        // Broadcast from server
        if (server.isRunning() && server.getClientCount() > 0) {
            ostringstream oss;
            oss << "Server broadcast #" << messageCount;
            server.broadcast(oss.str());
            addLog("[Server] Broadcast: " + oss.str());
        }

        // Send from client
        if (client.isConnected()) {
            ostringstream oss;
            oss << "Hello from client #" << messageCount;
            client.send(oss.str());
            addLog("[Client] Sent: " + oss.str());
        }
    } else if (key == 'D') {
        // Disconnect
        if (client.isConnected()) {
            client.disconnect();
            addLog("[Client] Disconnecting...");
        }
        if (server.isRunning()) {
            server.stop();
            addLog("[Server] Stopped");
        }
    } else if (key == 'X') {
        logMessages.clear();
        messageCount = 0;
        addLog("Log cleared");
    }
}

void tcApp::cleanup() {
    client.disconnect();
    server.stop();
}

void tcApp::addLog(const string& msg) {
    logNotice("tcApp") << msg;

    logMessages.push_back(msg);
    if (logMessages.size() > 20) {
        logMessages.erase(logMessages.begin());
    }
}
