#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include <set>
#include <deque>
#include <string>

using namespace trussc;

// keyboardExample - Keyboard Input Demo
// Visualization of key press state, modifier keys, and key codes

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;
    void keyReleased(int key) override;

private:
    // Currently pressed keys
    std::set<int> pressedKeys;

    // Last pressed key
    int lastKey = 0;

    // Key input history
    std::deque<std::string> keyHistory;
    static const size_t MAX_HISTORY = 15;

    // Box position (move with arrow keys)
    float boxX = 400;
    float boxY = 300;
    float boxSpeed = 5.0f;

    void addHistory(const std::string& msg);
    std::string keyToString(int key);
};
