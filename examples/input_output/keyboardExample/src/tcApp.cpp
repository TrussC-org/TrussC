#include "tcApp.h"
#include <sstream>

void tcApp::setup() {
    setWindowTitle("keyboardExample");
    addHistory("Press any key...");
}

void tcApp::addHistory(const std::string& msg) {
    keyHistory.push_back(msg);
    if (keyHistory.size() > MAX_HISTORY) {
        keyHistory.pop_front();
    }
}

std::string tcApp::keyToString(int key) {
    // 特殊キーの名前を返す
    switch (key) {
        case KEY_SPACE: return "SPACE";
        case KEY_ESCAPE: return "ESCAPE";
        case KEY_ENTER: return "ENTER";
        case KEY_TAB: return "TAB";
        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_DELETE: return "DELETE";
        case KEY_RIGHT: return "RIGHT";
        case KEY_LEFT: return "LEFT";
        case KEY_DOWN: return "DOWN";
        case KEY_UP: return "UP";
        case KEY_LEFT_SHIFT: return "L_SHIFT";
        case KEY_RIGHT_SHIFT: return "R_SHIFT";
        case KEY_LEFT_CONTROL: return "L_CTRL";
        case KEY_RIGHT_CONTROL: return "R_CTRL";
        case KEY_LEFT_ALT: return "L_ALT";
        case KEY_RIGHT_ALT: return "R_ALT";
        case KEY_LEFT_SUPER: return "L_CMD";
        case KEY_RIGHT_SUPER: return "R_CMD";
        case KEY_F1: return "F1";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";
        case KEY_F11: return "F11";
        case KEY_F12: return "F12";
        default:
            if (key >= 32 && key < 127) {
                return std::string(1, (char)key);
            }
            return "KEY_" + std::to_string(key);
    }
}

void tcApp::draw() {
    clear(30);

    // 矢印キーで移動するボックス
    if (pressedKeys.count(KEY_LEFT)) boxX -= boxSpeed;
    if (pressedKeys.count(KEY_RIGHT)) boxX += boxSpeed;
    if (pressedKeys.count(KEY_UP)) boxY -= boxSpeed;
    if (pressedKeys.count(KEY_DOWN)) boxY += boxSpeed;

    // ボックスを画面内に制限
    int w = getWindowWidth();
    int h = getWindowHeight();
    if (boxX < 25) boxX = 25;
    if (boxX > w - 25) boxX = w - 25;
    if (boxY < 25) boxY = 25;
    if (boxY > h - 25) boxY = h - 25;

    // ボックスを描画
    setColor(colors::blue);
    drawRect(boxX - 40, boxY - 40, 80, 80);

    setColor(255);
    drawBitmapString("Arrow keys\nto move", boxX - 35, boxY - 15);

    // タイトル
    setColor(255);
    drawBitmapString("=== Keyboard Input Demo ===", 20, 20);

    // 最後に押されたキー
    std::stringstream ss;
    ss << "Last key: " << keyToString(lastKey) << " (code: " << lastKey << ")";
    drawBitmapString(ss.str(), 20, 50);

    // 現在押されているキー
    setColor(150);
    drawBitmapString("Currently pressed:", 20, 80);

    setColor(50, 150, 50);  // 暗い緑
    std::stringstream pressed;
    for (int k : pressedKeys) {
        pressed << keyToString(k) << " ";
    }
    if (pressedKeys.empty()) {
        pressed << "(none)";
    }
    drawBitmapString(pressed.str(), 20, 95);

    // キー入力履歴
    setColor(100);
    drawBitmapString("Key History:", 20, 130);

    setColor(180);
    float y = 150;
    for (const auto& h : keyHistory) {
        drawBitmapString(h, 30, y);
        y += 15;
    }

    // 操作説明
    setColor(100);
    drawBitmapString("Use arrow keys to move the box", 20, h - 40);
    drawBitmapString("Press any key to see its code", 20, h - 25);
}

void tcApp::keyPressed(int key) {
    pressedKeys.insert(key);
    lastKey = key;

    std::stringstream ss;
    ss << "PRESSED: " << keyToString(key) << " (" << key << ")";
    addHistory(ss.str());
}

void tcApp::keyReleased(int key) {
    pressedKeys.erase(key);

    std::stringstream ss;
    ss << "RELEASED: " << keyToString(key);
    addHistory(ss.str());
}
