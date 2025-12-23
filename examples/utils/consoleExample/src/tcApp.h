#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>
#include <vector>
#include <deque>

using namespace std;

// =============================================================================
// consoleExample - Sample for receiving commands from stdin
// =============================================================================
// Commands can be sent from AI assistants or external processes.
//
// Usage (run from terminal):
//   ./consoleExample
//   >>> tcdebug info          # Get app info as JSON
//   >>> tcdebug screenshot /tmp/shot.png  # Take screenshot
//   >>> spawn 100 200         # Spawn a ball at (100, 200)
//   >>> clear                  # Clear all balls
//
// Send via pipe from external source:
//   echo "spawn 200 300" | ./consoleExample
//
// =============================================================================

// ---------------------------------------------------------------------------
// Ball - Simple ball
// ---------------------------------------------------------------------------
struct Ball {
    float x, y;
    Color color;
};

// ---------------------------------------------------------------------------
// tcApp - Main application
// ---------------------------------------------------------------------------
class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    vector<Ball> balls_;
    deque<string> commandLog_;
    EventListener consoleListener_;
};
