#include "tcApp.h"
#include "TrussC.h"

// =============================================================================
// setup - Initialization
// =============================================================================
void tcApp::setup() {
    setVsync(true);

    // Get device list
    serial.listDevices();
    deviceList = serial.getDeviceList();

    // Set target device path (e.g., Arduino)
    // serialDevicePath = "/dev/tty.usbserial-A10172HG";

    int baud = 9600;

    // Connect to specified device if available, otherwise connect to first device
    if (!serialDevicePath.empty()) {
        serial.setup(serialDevicePath, baud);
    } else if (!deviceList.empty()) {
        serial.setup(0, baud);
    }

    timeLastTryConnect = getElapsedTime();
    readTime = 0;
}

// =============================================================================
// update - Update Processing
// =============================================================================
void tcApp::update() {
    if (serial.isInitialized()) {
        // Send message
        if (bSendSerialMessage && !messageToSend.empty()) {
            serial.writeBytes(messageToSend);
            messageToSend.clear();
            bSendSerialMessage = false;
        }

        // Receive data
        int numBytesToRead = serial.available();
        if (numBytesToRead > 512) {
            numBytesToRead = 512;
        }

        if (numBytesToRead > 0) {
            std::string buffer;
            serial.readBytes(buffer, numBytesToRead);

            // Process received data (split by newline, otherwise add to buffer)
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

            // Display data even without newline (for Arduino sending without newlines)
            if (!serialReadBuffer.empty() && serialReadBuffer.length() >= 3) {
                receivedMessages.push_back(serialReadBuffer);
                serialReadBuffer.clear();
            }

            readTime = getElapsedTime();
        }
    } else {
        // If not connected, retry every 10 seconds
        float now = getElapsedTime();
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

    // Remove old messages (keep max 10)
    while (receivedMessages.size() > 10) {
        receivedMessages.erase(receivedMessages.begin());
    }
}

// =============================================================================
// draw - Drawing
// =============================================================================
void tcApp::draw() {
    clear(255);
    setColor(0.16f);

    // Connection status
    std::string connStr = "Serial connected: ";
    connStr += serial.isInitialized() ? "true" : "false";
    if (serial.isInitialized()) {
        connStr += " (" + serial.getDevicePath() + ")";
    }
    drawBitmapString(connStr, 50, 40);

    // Device list
    std::string deviceStr = "Devices:\n";
    for (const auto& dev : deviceList) {
        deviceStr += std::to_string(dev.getDeviceID()) + ": " + dev.getDevicePath() + "\n";
    }
    drawBitmapString(deviceStr, 50, 60);

    // Message to send
    std::string msgStr = "Type to send message\n";
    if (!messageToSend.empty()) {
        msgStr += messageToSend;
    }
    drawBitmapString(msgStr, 50, 400, 2.0f);

    // Received messages
    float posY = 60;
    drawBitmapString("Received messages", 550, posY, 2.0f);
    posY += 42;

    for (int i = (int)receivedMessages.size() - 1; i >= 0; i--) {
        // Highlight latest message
        if (i == (int)receivedMessages.size() - 1 && (getElapsedTime() - readTime) < 0.5f) {
            setColor(0.16f);
        } else {
            setColor(0.47f);
        }
        drawBitmapString(receivedMessages[i], 550, posY, 2.0f);
        posY += 42;
    }
}

// =============================================================================
// keyPressed - Key Input
// =============================================================================
void tcApp::keyPressed(int key) {
    // Enter: Send message
    if (key == KEY_ENTER) {
        if (!messageToSend.empty()) {
            bSendSerialMessage = true;
        }
    }
    // Backspace/Delete: Delete one character
    else if (key == KEY_BACKSPACE || key == KEY_DELETE || key == 27) {
        if (!messageToSend.empty()) {
            messageToSend.pop_back();
        }
    }
    // Escape: Clear message
    else if (key == KEY_ESCAPE) {
        messageToSend.clear();
    }
    // Regular characters (sokol_app key codes are same as uppercase ASCII, so convert to lowercase)
    else if (key >= 32 && key < 127) {
        char c = static_cast<char>(key);
        // Convert A-Z (65-90) to lowercase
        if (c >= 'A' && c <= 'Z') {
            c = c + 32;  // 'a' - 'A' = 32
        }
        messageToSend += c;
    }
}
