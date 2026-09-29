// =============================================================================
// tcApp.cpp - Multi-window example
// =============================================================================
// Press W to open a second window. It runs on its own vsync tick (its
// display's refresh rate), has its own Node tree, events and mouse, and can
// draw GPU resources from the main window directly (shared sokol_gfx context).
// Closing the second window leaves the main window running.
// Secondary windows: macOS / Windows / Linux; elsewhere W logs an error.
//
// Arrow keys move the second window with Window::setPosition(); F sends it
// fullscreen. Both act on THAT window — the global setWindowPosition() /
// setFullscreen() target the main one. For a projector or a video wall on a
// second display, move the window onto that display FIRST and then go
// fullscreen: fullscreen covers the display the window is currently on.

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

    if (second && second->isOpen()) {
        IVec2 p = second->getPosition();
        drawBitmapString("second window at (" + toString(p.x) + ", " + toString(p.y) + ")"
                         + (second->isFullscreen() ? "  [fullscreen]" : ""), 20, 66);
        setColor(0.7f);
        drawBitmapString("arrows: move it by 50px   F: fullscreen it", 20, 340);
    }

    // The same FBO also drawn here
    setColor(1.0f);
    fbo.draw(20, 80, 320, 240);

    // Main window's own mouse
    setColor(0.2f, 1.0f, 0.5f);
    drawCircle(getMouseX(), getMouseY(), 12);
}

void tcApp::keyPressed(int key) {
    // Drive the SECOND window from the main one. These are Window methods, so
    // they act on that window; the global equivalents would move/fullscreen the
    // main window instead.
    if (second && second->isOpen()) {
        const int step = 50;
        IVec2 p = second->getPosition();
        if (key == KEY_LEFT)  { second->setPosition(p.x - step, p.y); return; }
        if (key == KEY_RIGHT) { second->setPosition(p.x + step, p.y); return; }
        if (key == KEY_UP)    { second->setPosition(p.x, p.y - step); return; }
        if (key == KEY_DOWN)  { second->setPosition(p.x, p.y + step); return; }
        if (key == 'F')       { second->toggleFullscreen(); return; }
        if (key == 'G')       { logNotice("tcApp") << "second window at " << p.x << ", " << p.y; return; }
    }

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
            subApp = make_shared<SubApp>();
            subApp->sharedFbo = &fbo;
            second->setApp(subApp);
        }
    }
}
