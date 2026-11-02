#include "tcApp.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace {
bool parseNumber(const string& text, float& value) {
    char* end = nullptr;
    errno = 0;
    value = std::strtof(text.c_str(), &end);
    // Require a complete, finite number within the float range.
    return end != text.c_str() && end == text.c_str() + text.size() &&
           errno == 0 && std::isfinite(value);
}
} // namespace

void tcApp::setup() {
    // Listen to console events (retain the listener)
    consoleListener_ = runtime().events().console.listen([this](ConsoleEventArgs& e) {
        // Add to log (keep latest 10 entries)
        commandLog_.push_back(e.raw);
        if (commandLog_.size() > 10) {
            commandLog_.pop_front();
        }

        // Process custom commands
        if (e.args.empty()) return;

        if (e.args[0] == "spawn" && e.args.size() >= 3) {
            // spawn x y - Spawn a ball
            float x, y;
            if (!parseNumber(e.args[1], x) || !parseNumber(e.args[2], y)) {
                // JSON output for external process communication (intentionally using cout)
                cout << "{\"status\":\"error\",\"command\":\"spawn\",\"message\":\"Expected numeric x and y\"}" << endl;
                return;
            }
            Ball ball;
            ball.x = x;
            ball.y = y;
            ball.color = ColorHSB(random(1.0f), 0.8f, 0.9f).toRGB();
            balls_.push_back(ball);
            // JSON output for external process communication (intentionally using cout)
            cout << "{\"status\":\"ok\",\"command\":\"spawn\",\"ballCount\":" << balls_.size() << "}" << endl;
        }
        else if (e.args[0] == "clear") {
            // clear - Clear all balls
            balls_.clear();
            // JSON output for external process communication (intentionally using cout)
            cout << "{\"status\":\"ok\",\"command\":\"clear\"}" << endl;
        }
    });

    console::start();  // Opt in to stdin commands.

    // JSON output for external process communication (intentionally using cout)
    cout << "consoleExample started. Ready for commands." << endl;
    cout << "Try: tcdebug info, spawn 100 200, clear" << endl;
}

void tcApp::draw() {
    clear(0.12f);

    // Display usage instructions
    setColor(0.78f);
    drawBitmapString("Console Example - stdin commands", 20, 30);
    drawBitmapString("Commands:", 20, 60);
    drawBitmapString("  tcdebug info          - Get app info (JSON)", 20, 80);
    drawBitmapString("  tcdebug screenshot    - Take screenshot", 20, 100);
    drawBitmapString("  spawn x y             - Spawn a ball", 20, 120);
    drawBitmapString("  clear                 - Clear all balls", 20, 140);

    // Display command log
    setColor(0.59f);
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
    setColor(1.0f);
    drawBitmapString("Balls: " + to_string(balls_.size()), 20, getHeight() - 30);
}
