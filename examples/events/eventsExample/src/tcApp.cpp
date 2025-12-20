#include "tcApp.h"
#include <sstream>

void tcApp::setup() {
    setWindowTitle("eventsExample");
    setupListeners();
    addLog("Event system demo started");
    addLog("Press 'k' to toggle key listener");
    addLog("Press 'm' to toggle mouse listener");
    addLog("");
}

void tcApp::setupListeners() {
    // Key event listener
    if (keyListenerActive) {
        events().keyPressed.listen(keyListener, [this](KeyEventArgs& e) {
            std::stringstream ss;
            ss << "[KeyEvent] key=" << e.key;
            if (e.shift) ss << " +Shift";
            if (e.ctrl) ss << " +Ctrl";
            if (e.alt) ss << " +Alt";
            if (e.super) ss << " +Cmd";
            if (e.isRepeat) ss << " (repeat)";
            addLog(ss.str());
        });
    }

    // Mouse click listener
    if (mouseListenerActive) {
        events().mousePressed.listen(mouseListener, [this](MouseEventArgs& e) {
            std::stringstream ss;
            ss << "[MouseEvent] button=" << e.button;
            ss << " pos=(" << (int)e.x << "," << (int)e.y << ")";
            addLog(ss.str());
        });
    }

    // Scroll listener (always enabled)
    events().mouseScrolled.listen(scrollListener, [this](ScrollEventArgs& e) {
        std::stringstream ss;
        ss << "[ScrollEvent] dx=" << e.scrollX << " dy=" << e.scrollY;
        addLog(ss.str());
    });
}

void tcApp::addLog(const std::string& msg) {
    eventLog.push_back(msg);
    if (eventLog.size() > MAX_LOG_LINES) {
        eventLog.pop_front();
    }
}

void tcApp::draw() {
    clear(30);
    setColor(1.0f);

    // Title
    drawBitmapString("=== Event System Demo ===", 20, 20);

    // Status display
    std::stringstream status;
    status << "Key Listener: " << (keyListenerActive ? "ON" : "OFF");
    status << "  |  Mouse Listener: " << (mouseListenerActive ? "ON" : "OFF");
    drawBitmapString(status.str(), 20, 50);

    // Controls description
    setColor(0.6f);
    drawBitmapString("Press 'k' to toggle key listener", 20, 80);
    drawBitmapString("Press 'm' to toggle mouse listener", 20, 95);
    drawBitmapString("Click anywhere or scroll to test events", 20, 110);

    // Event log
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString("Event Log:", 20, 150);

    setColor(0.78f);
    float y = 170;
    for (const auto& log : eventLog) {
        drawBitmapString(log, 30, y);
        y += 15;
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'k' || key == 'K') {
        keyListenerActive = !keyListenerActive;

        // Disconnect or reconnect listener
        if (keyListenerActive) {
            events().keyPressed.listen(keyListener, [this](KeyEventArgs& e) {
                std::stringstream ss;
                ss << "[KeyEvent] key=" << e.key;
                if (e.shift) ss << " +Shift";
                if (e.ctrl) ss << " +Ctrl";
                if (e.alt) ss << " +Alt";
                if (e.super) ss << " +Cmd";
                addLog(ss.str());
            });
            addLog(">> Key listener ENABLED");
        } else {
            keyListener.disconnect();
            addLog(">> Key listener DISABLED");
        }
    }
    else if (key == 'm' || key == 'M') {
        mouseListenerActive = !mouseListenerActive;

        if (mouseListenerActive) {
            events().mousePressed.listen(mouseListener, [this](MouseEventArgs& e) {
                std::stringstream ss;
                ss << "[MouseEvent] button=" << e.button;
                ss << " pos=(" << (int)e.x << "," << (int)e.y << ")";
                addLog(ss.str());
            });
            addLog(">> Mouse listener ENABLED");
        } else {
            mouseListener.disconnect();
            addLog(">> Mouse listener DISABLED");
        }
    }
}
