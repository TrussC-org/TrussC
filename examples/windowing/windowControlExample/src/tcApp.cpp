// =============================================================================
// tcApp.cpp - windowControlExample (main window)
// =============================================================================
// The main window opens one SECONDARY window at setup and shows instructions.
//   Main window keys:  g -> toggle fullscreen on the MAIN window.
//   Secondary window keys (focus the blue window): f/s/r/1/2/3 (see SubApp).
// The secondary window is where the per-window routing is exercised: its key
// handlers call the context-aware global functions, which route to it because
// its tick is the one running.
// Secondary windows: macOS / Windows / Linux; elsewhere createWindow logs an error.

#include "tcApp.h"

void tcApp::setup() {
    logNotice("tcApp") << "MAIN window: 'g' toggles MAIN fullscreen. "
                          "Focus the blue window for f/s/r/1/2/3.";

    WindowSettings ws;
    ws.setSize(500, 400);
    ws.setTitle("windowControlExample - secondary");
    second_ = createWindow(ws);
    if (!second_) {
        logError("tcApp") << "createWindow failed (single-window platform?)";
        return;
    }
    second_->setClearColor(Color(0.10f, 0.20f, 0.72f));   // BLUE background
    subApp_ = make_shared<SubApp>();
    subApp_->self = second_.get();
    second_->setApp(subApp_);
    logNotice("tcApp") << "secondary window created";
}

void tcApp::draw() {
    clear(0.10f, 0.10f, 0.12f);   // dark background

    // RED badge
    setColor(0.9f, 0.15f, 0.15f);
    drawRect(20, 20, 40, 40);

    setColor(1.0f);
    drawBitmapString("windowControlExample - MAIN window", 80, 34);
    drawBitmapString("g: toggle fullscreen on THIS (main) window", 80, 52);
    drawBitmapString(string("main fullscreen: ") + (isFullscreen() ? "ON" : "off"), 80, 68);

    drawBitmapString("secondary window (blue): " +
                     string(second_ && second_->isOpen() ? "OPEN" : "closed"), 20, 120);
    drawBitmapString("focus the blue window, then:", 20, 140);
    drawBitmapString("  f fullscreen | s screenshot | r record 3s | 1/2/3 fps", 20, 156);

    // Main window's own mouse
    setColor(0.2f, 1.0f, 0.5f);
    drawCircle(getMouseX(), getMouseY(), 10);
}

void tcApp::keyPressed(int key) {
    if (key == 'G') {
        toggleFullscreen();   // main context -> main window (sokol_app path)
        logNotice("tcApp") << "main toggleFullscreen -> isFullscreen=" << isFullscreen();
    }
}
