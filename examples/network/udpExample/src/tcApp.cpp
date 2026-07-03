// =============================================================================
// tcApp.cpp - UDP Socket Example
// =============================================================================

#include "tcApp.h"
#include <sstream>

using namespace std;
using namespace trussc;

void tcApp::setup() {
    logNotice("tcApp") << "=== UDP Socket Example ===";
    logNotice("tcApp") << "Press SPACE to send a message";
    logNotice("tcApp") << "Press C to clear messages";
    logNotice("tcApp") << "==========================";

    // Listen for receive events.
    // onReceive fires on the receive thread; Deliver::Main marshals the
    // listener onto the main thread, so app state can be touched without locks.
    receiveListener = receiver.onReceive.listen([this](UdpReceiveEventArgs& e) {
        string msg(e.data.begin(), e.data.end());
        logNotice("UdpReceiver") << "Received from " << e.remoteHost << ":" << e.remotePort << " -> " << msg;

        receivedMessages.push_back(e.remoteHost + ":" + to_string(e.remotePort) + " -> " + msg);
        if (receivedMessages.size() > 20) {
            receivedMessages.erase(receivedMessages.begin());
        }
    }, Deliver::Main);

    // Listen for error events
    errorListener = receiver.onError.listen([](UdpErrorEventArgs& e) {
        logError("UdpReceiver") << "UDP Error: " << e.message;
    });

    // Bind the receiver socket (start receiving on port 9000)
    if (!receiver.bind(9000)) {
        logError("tcApp") << "Failed to bind receiver to port 9000";
    }

    // Set the destination for the sender socket
    sender.connect("127.0.0.1", 9000);
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;

    // Title
    setColor(1.0f);
    drawBitmapString("UDP Socket Example", 40, y);
    y += 30;

    // Instructions
    setColor(0.7f);
    drawBitmapString("SPACE: Send message   C: Clear", 40, y);
    y += 40;

    // Send count
    setColor(0.4f, 0.78f, 1.0f);
    drawBitmapString("Sent: " + to_string(sendCount) + " messages", 40, y);
    y += 30;

    // Received messages
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString("Received Messages:", 40, y);
    y += 25;

    setColor(0.86f);
    for (const auto& msg : receivedMessages) {
        drawBitmapString(msg, 50, y);
        y += 18;
    }
}

void tcApp::keyPressed(int key) {
    if (key == KEY_SPACE || key == ' ') {
        // Send message
        sendCount++;
        ostringstream oss;
        oss << "Hello from TrussC! #" << sendCount;
        string msg = oss.str();

        if (sender.send(msg)) {
            logNotice("tcApp") << "Sent: " << msg;
        }
    } else if (key == 'C') {
        receivedMessages.clear();
        sendCount = 0;
        logNotice("tcApp") << "Messages cleared";
    }
}

void tcApp::cleanup() {
    receiver.close();
    sender.close();
}
