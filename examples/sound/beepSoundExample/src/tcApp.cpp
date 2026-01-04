// =============================================================================
// beepSoundExample - Debug Beep Sound Test
//
// Test dbg::beep() functions for debugging audio feedback
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    logNotice("beepSoundExample") << "=== dbg::beep() Test ===";
    logNotice("beepSoundExample") << "Keys 1-9, 0, - for presets";
    logNotice("beepSoundExample") << "UP/DOWN: Volume control";
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;

    // Title
    setColor(colors::white);
    drawBitmapString("dbg::beep() Test", 50, y);
    y += 35;

    // Categories
    setColor(0.5f);
    drawBitmapString("[Basic]", 50, y); y += 20;
    setColor(0.7f);
    drawBitmapString("1 - ping (default)", 70, y); y += 18;
    y += 8;

    setColor(0.5f);
    drawBitmapString("[Positive]", 50, y); y += 20;
    setColor(0.7f);
    drawBitmapString("2 - success (pico)", 70, y); y += 18;
    drawBitmapString("3 - complete (fanfare)", 70, y); y += 18;
    drawBitmapString("4 - coin (sparkly)", 70, y); y += 18;
    y += 8;

    setColor(0.5f);
    drawBitmapString("[Negative]", 50, y); y += 20;
    setColor(0.7f);
    drawBitmapString("5 - error (boo)", 70, y); y += 18;
    drawBitmapString("6 - warning", 70, y); y += 18;
    drawBitmapString("7 - cancel", 70, y); y += 18;
    y += 8;

    setColor(0.5f);
    drawBitmapString("[UI Feedback]", 50, y); y += 20;
    setColor(0.7f);
    drawBitmapString("8 - click", 70, y); y += 18;
    drawBitmapString("9 - typing", 70, y); y += 18;
    drawBitmapString("0 - notify", 70, y); y += 18;
    y += 8;

    setColor(0.5f);
    drawBitmapString("[Transition]", 50, y); y += 20;
    setColor(0.7f);
    drawBitmapString("- - sweep (whoosh)", 70, y); y += 18;
    y += 15;

    setColor(0.5f);
    drawBitmapString("UP/DOWN - Volume", 70, y); y += 25;

    // Volume display
    setColor(colors::white);
    drawBitmapString(format("Volume: {:.0f}%", dbg::getBeepVolume() * 100), 50, y);
    y += 25;

    // Volume bar
    setColor(0.3f);
    drawRect(50, y, 200, 14);
    setColor(colors::lime);
    drawRect(50, y, 200 * dbg::getBeepVolume(), 14);

    // FPS
    setColor(0.4f);
    drawBitmapString(format("FPS: {:.0f}", getFrameRate()), 50, getWindowHeight() - 30);
}

void tcApp::keyPressed(int key) {
    // Basic
    if (key == '1') {
        dbg::beep(dbg::Beep::ping);
        logNotice("beep") << "ping";
    }
    // Positive
    else if (key == '2') {
        dbg::beep(dbg::Beep::success);
        logNotice("beep") << "success";
    }
    else if (key == '3') {
        dbg::beep(dbg::Beep::complete);
        logNotice("beep") << "complete";
    }
    else if (key == '4') {
        dbg::beep(dbg::Beep::coin);
        logNotice("beep") << "coin";
    }
    // Negative
    else if (key == '5') {
        dbg::beep(dbg::Beep::error);
        logNotice("beep") << "error";
    }
    else if (key == '6') {
        dbg::beep(dbg::Beep::warning);
        logNotice("beep") << "warning";
    }
    else if (key == '7') {
        dbg::beep(dbg::Beep::cancel);
        logNotice("beep") << "cancel";
    }
    // UI Feedback
    else if (key == '8') {
        dbg::beep(dbg::Beep::click);
        logNotice("beep") << "click";
    }
    else if (key == '9') {
        dbg::beep(dbg::Beep::typing);
        logNotice("beep") << "typing";
    }
    else if (key == '0') {
        dbg::beep(dbg::Beep::notify);
        logNotice("beep") << "notify";
    }
    // Transition
    else if (key == '-') {
        dbg::beep(dbg::Beep::sweep);
        logNotice("beep") << "sweep";
    }
    // Volume control
    else if (key == KEY_UP) {
        float vol = dbg::getBeepVolume() + 0.1f;
        dbg::setBeepVolume(vol);
        dbg::beep();
        logNotice("beep") << "Volume: " << (int)(dbg::getBeepVolume() * 100) << "%";
    }
    else if (key == KEY_DOWN) {
        float vol = dbg::getBeepVolume() - 0.1f;
        dbg::setBeepVolume(vol);
        dbg::beep();
        logNotice("beep") << "Volume: " << (int)(dbg::getBeepVolume() * 100) << "%";
    }
}

void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(Vec2 pos, int button) {
    dbg::beep();
}

void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
