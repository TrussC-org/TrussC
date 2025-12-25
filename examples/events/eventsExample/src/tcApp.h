#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// eventsExample - Event system demo
// Demonstrates EventListener RAII behavior and event subscription

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Event listeners (stored as members)
    EventListener keyListener;
    EventListener mouseListener;
    EventListener scrollListener;

    // Event log
    std::deque<std::string> eventLog;
    static const size_t MAX_LOG_LINES = 20;

    // Listener active states
    bool keyListenerActive = true;
    bool mouseListenerActive = true;

    void addLog(const std::string& msg);
    void setupListeners();
};
