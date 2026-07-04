// =============================================================================
// tcApp.cpp - Multi-window example
// =============================================================================
// Press W to open a second window. It runs on its own display link (its
// display's refresh rate), has its own Node tree, events and mouse, and can
// draw GPU resources from the main window directly (shared sokol_gfx context).
// Closing the second window leaves the main window running.
// Secondary windows are macOS-only for now; on other platforms W logs an error.

#include "tcApp.h"

void tcApp::setup() {
    logNotice("tcApp") << "Press W to open/close the second window";
    fbo.allocate(320, 240);
}

void tcApp::update() {
    t += getDeltaTime();

    // Animate something into the shared FBO every main-window frame
    fbo.begin();
    clear(0.1f, 0.1f, 0.15f);
    setColor(Color::fromHSB(fmodf(t * 0.1f, 1.0f), 0.7f, 1.0f));
    float cx = 160 + cosf(t * TAU * 0.25f) * 100;
    float cy = 120 + sinf(t * TAU * 0.4f) * 60;
    drawCircle(cx, cy, 40);
    fbo.end();
}

void tcApp::draw() {
    clear(0.12f);

    setColor(1.0f);
    drawBitmapString("MAIN window  (W: toggle second window)", 20, 30);
    drawBitmapString(second && second->isOpen() ? "second window: OPEN" : "second window: closed", 20, 50);

    // The same FBO also drawn here
    setColor(1.0f);
    fbo.draw(20, 80, 320, 240);

    // Main window's own mouse
    setColor(0.2f, 1.0f, 0.5f);
    drawCircle(getMouseX(), getMouseY(), 12);
}

void tcApp::keyPressed(int key) {
    if (key == 'W') {
        if (second && second->isOpen()) {
            logNotice("tcApp") << "closing second window";
            second->close();
            return;
        }
        WindowSettings ws;
        ws.setSize(480, 320);
        ws.setTitle("multiWindowExample - second");
        second = createWindow(ws);
        logNotice("tcApp") << (second ? "second window created" : "createWindow failed");
        if (second) {
            subScene = make_shared<SubScene>();
            subScene->sharedFbo = &fbo;
            second->setRoot(subScene);
        }
    }
}
