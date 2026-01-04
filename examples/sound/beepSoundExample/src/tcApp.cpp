// =============================================================================
// beepSoundExample - Debug Beep Sound Test
//
// Test dbg::beep() functions for debugging audio feedback
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    logNotice("beepSoundExample") << "=== dbg::beep() Test ===";
    logNotice("beepSoundExample") << "1: ping (default)";
    logNotice("beepSoundExample") << "2: success (pico)";
    logNotice("beepSoundExample") << "3: error (boo)";
    logNotice("beepSoundExample") << "4: notify (ping-pong)";
    logNotice("beepSoundExample") << "A-G: Musical notes";
    logNotice("beepSoundExample") << "SPACE: Rapid fire test (debounce)";
    logNotice("beepSoundExample") << "UP/DOWN: Volume control";
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f);

    float y = 50;

    // Title
    setColor(colors::white);
    drawBitmapString("dbg::beep() Test", 50, y);
    y += 40;

    // Controls
    setColor(0.7f);
    drawBitmapString("Press keys to test sounds:", 50, y);
    y += 30;

    drawBitmapString("1 - ping (default beep)", 70, y); y += 22;
    drawBitmapString("2 - success (pico)", 70, y); y += 22;
    drawBitmapString("3 - error (boo)", 70, y); y += 22;
    drawBitmapString("4 - notify (ping-pong)", 70, y); y += 22;
    y += 10;

    drawBitmapString("A-G - Musical notes (A4-G4)", 70, y); y += 22;
    y += 10;

    drawBitmapString("SPACE - Rapid fire test", 70, y); y += 22;
    drawBitmapString("UP/DOWN - Volume", 70, y); y += 22;
    y += 20;

    // Volume display
    setColor(colors::white);
    drawBitmapString(format("Volume: {:.0f}%", dbg::getBeepVolume() * 100), 50, y);
    y += 30;

    // Volume bar
    setColor(0.3f);
    drawRect(50, y, 200, 20);
    setColor(colors::lime);
    drawRect(50, y, 200 * dbg::getBeepVolume(), 20);
    y += 50;

    // Debounce info
    setColor(0.5f);
    drawBitmapString("Note: Same-frame calls are debounced", 50, y);
    y += 22;
    drawBitmapString("(only one beep per frame)", 50, y);

    // FPS
    setColor(0.4f);
    drawBitmapString(format("FPS: {:.0f}", getFrameRate()), 50, getWindowHeight() - 40);
}

void tcApp::keyPressed(int key) {
    // Preset sounds
    if (key == '1') {
        dbg::beep();  // or dbg::beep(dbg::Beep::ping)
        logNotice("beep") << "ping";
    }
    else if (key == '2') {
        dbg::beep(dbg::Beep::success);
        logNotice("beep") << "success";
    }
    else if (key == '3') {
        dbg::beep(dbg::Beep::error);
        logNotice("beep") << "error";
    }
    else if (key == '4') {
        dbg::beep(dbg::Beep::notify);
        logNotice("beep") << "notify";
    }
    // Musical notes (A4 = 440Hz)
    else if (key == 'a' || key == 'A') {
        dbg::beep(440);   // A4
        logNotice("beep") << "A4 (440Hz)";
    }
    else if (key == 'b' || key == 'B') {
        dbg::beep(494);   // B4
        logNotice("beep") << "B4 (494Hz)";
    }
    else if (key == 'c' || key == 'C') {
        dbg::beep(523);   // C5
        logNotice("beep") << "C5 (523Hz)";
    }
    else if (key == 'd' || key == 'D') {
        dbg::beep(587);   // D5
        logNotice("beep") << "D5 (587Hz)";
    }
    else if (key == 'e' || key == 'E') {
        dbg::beep(659);   // E5
        logNotice("beep") << "E5 (659Hz)";
    }
    else if (key == 'f' || key == 'F') {
        dbg::beep(698);   // F5
        logNotice("beep") << "F5 (698Hz)";
    }
    else if (key == 'g' || key == 'G') {
        dbg::beep(784);   // G5
        logNotice("beep") << "G5 (784Hz)";
    }
    // Rapid fire test (debounce)
    else if (key == ' ') {
        // Call beep multiple times - should only play once
        dbg::beep();
        dbg::beep();
        dbg::beep();
        logNotice("beep") << "SPACE pressed (3x beep called, debounced to 1)";
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
    // Click to beep
    dbg::beep();
}

void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}
void tcApp::filesDropped(const vector<string>& files) {}
void tcApp::exit() {}
