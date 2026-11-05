// =============================================================================
// tcApp.cpp - Shared FBO example
// =============================================================================
// One wide Fbo holds the whole picture. It is animated ONCE per frame, in the
// main window's update(). Three windows (this one plus two more) each show
// one third of it - left, center, right - so placed side by side they read
// as one picture: a multi-window video wall.
//
// R (in any of the three windows) puts them side by side again after you
// move them, with a small gap so the window frames don't overlap.
// Closing any window quits (setQuitOnClose on all three).
// Secondary windows: macOS / Windows / Linux; elsewhere only the left third shows.

#include "tcApp.h"

void tcApp::setup() {
    wall_->allocate(PANEL_W * PANEL_COUNT, PANEL_H);

    for (int i = 1; i < PANEL_COUNT; i++) {
        auto win = createWindow(WindowSettings()
            .setSize(PANEL_W, PANEL_H)
            .setTitle(string("sharedFboExample - ") + PANEL_NAMES[i])
            .setQuitOnClose(true));
        if (!win) {
            logError("tcApp") << "createWindow failed (single-window platform?)";
            break;
        }
        // Every window closes the app (setQuitOnClose), so the panels never
        // outlive this App and can call back into it.
        win->setApp<PanelApp>(wall_, i, [this] { arrange(); });
        panels_.push_back(win);
    }
    arrange();
    logNotice("tcApp") << "R: put the windows side by side again";
}

void tcApp::update() {
    const float t = getElapsedTimef();
    const float w = (float)wall_->getWidth();
    const float h = (float)wall_->getHeight();

    wall_->begin();
    clear(0.05f, 0.06f, 0.10f);

    // A wave that runs across the whole wall
    const int dots = 72;
    for (int i = 0; i < dots; i++) {
        const float x = w * (i + 0.5f) / dots;
        const float y = h * 0.5f + sinf(x / w * TAU * 2.0f + t * TAU * 0.3f) * h * 0.3f;
        setColor(Color::fromHSB((float)i / dots, 0.6f, 1.0f));
        drawCircle(x, y, 7);
    }

    // A ball that travels from window to window
    const float r = 36.0f;
    const float bx = fmodf(t * 240.0f, w + r * 2) - r;
    const float by = h * 0.5f + cosf(t * TAU * 0.5f) * h * 0.25f;
    setColor(1.0f);
    drawCircle(bx, by, r);
    wall_->end();
}

void tcApp::draw() {
    drawWallPart(*wall_, 0);
}

void tcApp::keyPressed(int key) {
    if (key == 'R') arrange();
}

void tcApp::arrange() {
    // Move every window through its handle: arrange() also runs from the
    // panels' keyPressed(), where setWindowPosition() would move the panel.
    const int gap = 16;
    const IVec2 origin{80, 120};
    getWindow()->setPosition(origin.x, origin.y);
    for (size_t i = 0; i < panels_.size(); i++) {
        if (panels_[i]->isOpen()) panels_[i]->setPosition(origin.x + (int)(i + 1) * (PANEL_W + gap), origin.y);
    }
}
