// =============================================================================
// tcApp.cpp - Dialog window example
// =============================================================================
// Click the main window to add circles. Press C to ask "Clear all circles?"
// in a small confirmation window in front of the main one.
//
// The dialog is app-side, not an OS modal: while it is open the main window
// ignores clicks and keys, raises the dialog again and flashes its background.
// OK closes the dialog and the main App gets `true`; closing it with the OS
// close button (x) counts as cancel (closeRequested) and the main App gets
// `false`.
// Secondary windows: macOS / Windows / Linux; elsewhere C logs an error.

#include "tcApp.h"

void tcApp::setup() {
    logNotice("tcApp") << "Click to add circles, press C to clear them (asks first)";
}

void tcApp::update() {
    // Ease toward the target; once near the peak, fall back to dark.
    bgBrightness_ += (bgTarget_ - bgBrightness_) * 0.1f;
    if (bgTarget_ > 0.0f && bgBrightness_ > 0.6f) bgTarget_ = 0.0f;
}

void tcApp::draw() {
    clear(0.12f + bgBrightness_ * 0.5f, 0.12f + bgBrightness_ * 0.5f, 0.15f + bgBrightness_ * 0.5f);

    setColor(1.0f, 0.6f, 0.2f);
    for (auto& c : circles_) drawCircle(c.x, c.y, 14);

    setColor(1.0f);
    drawBitmapString("MAIN window  (click: add circle   C: clear all circles)", 20, 30);
    drawBitmapString("circles: " + toString(circles_.size()) + "   last answer: " + lastAnswer_, 20, 50);
    if (dialogOpen()) {
        setColor(1.0f, 0.85f, 0.3f);
        drawBitmapString("dialog open: answer it first", 20, 70);
    }
}

void tcApp::keyPressed(int key) {
    if (dialogOpen()) { blockedInput(); return; }
    if (key == 'C') openDialog();
}

void tcApp::mousePressed(Vec2 pos, int button) {
    if (dialogOpen()) { blockedInput(); return; }
    if (button == MOUSE_BUTTON_LEFT) circles_.push_back(pos);
}

void tcApp::openDialog() {
    WindowSettings ws;
    ws.setSize(320, 140);
    ws.setTitle("Confirm");
    dialog_ = createWindow(ws);
    if (!dialog_) {
        logError("tcApp") << "createWindow failed (single-window platform?)";
        return;
    }

    // The callback is how the dialog's App hands the answer back to this App.
    dialog_->setApp<ConfirmApp>("Clear all " + toString(circles_.size()) + " circles?",
                                [this](bool ok) {
        lastAnswer_ = ok ? "OK" : "cancel";
        if (ok) circles_.clear();
        logNotice("tcApp") << "dialog answered: " << lastAnswer_;
    });

    // Center the dialog over the main window.
    const IVec2 p = getWindowPosition();
    dialog_->setPosition(p.x + (getWindowWidth() - ws.width) / 2,
                         p.y + (getWindowHeight() - ws.height) / 2);
}

void tcApp::blockedInput() {
    // Window::bringToFront() raises THAT window, even from the main window's
    // callbacks (the global bringWindowToFront() would raise this one).
    dialog_->bringToFront();
    bgTarget_ = 1.0f;
}
