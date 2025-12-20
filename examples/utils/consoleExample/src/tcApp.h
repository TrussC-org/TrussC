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
    void setup() override {
        // Listen to console events (retain the listener)
        events().console.listen(consoleListener_, [this](ConsoleEventArgs& e) {
            // Add to log (keep latest 10 entries)
            commandLog_.push_back(e.raw);
            if (commandLog_.size() > 10) {
                commandLog_.pop_front();
            }

            // Process custom commands
            if (e.args.empty()) return;

            if (e.args[0] == "spawn" && e.args.size() >= 3) {
                // spawn x y - Spawn a ball
                float x = stof(e.args[1]);
                float y = stof(e.args[2]);
                Ball ball;
                ball.x = x;
                ball.y = y;
                ball.color = ColorHSB(random(TAU), 0.8f, 0.9f).toRGB();
                balls_.push_back(ball);
                cout << "{\"status\":\"ok\",\"command\":\"spawn\",\"ballCount\":" << balls_.size() << "}" << endl;
            }
            else if (e.args[0] == "clear") {
                // clear - Clear all balls
                balls_.clear();
                cout << "{\"status\":\"ok\",\"command\":\"clear\"}" << endl;
            }
        });

        cout << "consoleExample started. Ready for commands." << endl;
        cout << "Try: tcdebug info, spawn 100 200, clear" << endl;
    }

    void draw() override {
        clear(30);

        // Display usage instructions
        setColor(200);
        drawBitmapString("Console Example - stdin commands", 20, 30);
        drawBitmapString("Commands:", 20, 60);
        drawBitmapString("  tcdebug info          - Get app info (JSON)", 20, 80);
        drawBitmapString("  tcdebug screenshot    - Take screenshot", 20, 100);
        drawBitmapString("  spawn x y             - Spawn a ball", 20, 120);
        drawBitmapString("  clear                 - Clear all balls", 20, 140);

        // Display command log
        setColor(150);
        drawBitmapString("Recent commands:", 20, 180);
        int y = 200;
        for (const auto& cmd : commandLog_) {
            drawBitmapString("> " + cmd, 20, y);
            y += 16;
        }

        // Draw balls
        for (const auto& ball : balls_) {
            setColor(ball.color);
            drawCircle(ball.x, ball.y, 20);
        }

        // Display ball count
        setColor(255);
        drawBitmapString("Balls: " + to_string(balls_.size()), 20, getWindowHeight() - 30);
    }

private:
    vector<Ball> balls_;
    deque<string> commandLog_;
    EventListener consoleListener_;
};
