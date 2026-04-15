#include "tcApp.h"

void tcApp::setup() {
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);

    // Show current window position
    auto pos = getWindowPosition();
    string info = "Window Position: (" + to_string(pos.x) + ", " + to_string(pos.y) + ")";
    drawBitmapString(info, 20, 40);

    drawBitmapString("Arrow keys: move window by 50px", 20, 80);
    drawBitmapString("R: reset to (100, 100)", 20, 110);

    // Window position API is only implemented on macOS and Windows
    if (pos.x == -1 && pos.y == -1) {
        setColor(colors::yellow);
        drawBitmapString("* This platform does not support window position API", 20, 160);
    }
}

void tcApp::keyPressed(int key) {
    auto pos = getWindowPosition();
    int step = 50;

    if (key == KEY_LEFT)  setWindowPosition(pos.x - step, pos.y);
    if (key == KEY_RIGHT) setWindowPosition(pos.x + step, pos.y);
    if (key == KEY_UP)    setWindowPosition(pos.x, pos.y - step);
    if (key == KEY_DOWN)  setWindowPosition(pos.x, pos.y + step);
    if (key == 'R' || key == 'r') setWindowPosition(100, 100);
}

void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(Vec2 pos, int button) {}
void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
