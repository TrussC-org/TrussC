// =============================================================================
// tcApp.cpp - Shared FBO example
// =============================================================================
// One wide Fbo holds the whole picture. It is animated ONCE per frame, in the
// main window's update(). Three windows (this one plus two more) each show
// one third of it - left, center, right - so placed side by side they read
// as one picture: a multi-window video wall.
//
// R puts the windows side by side again after you move them. The gap between
// them stands in for the bezels of a real video wall.
// Closing any window quits (setQuitOnClose on all three).
// Secondary windows: macOS / Windows / Linux; elsewhere only the left third shows.

#include "tcApp.h"

void tcApp::setup() {
    wall_->allocate(PANEL_W * PANEL_COUNT, PANEL_H);

    for (int i = 1; i < PANEL_COUNT; i++) {
        WindowSettings ws;
        ws.setSize(PANEL_W, PANEL_H);
        ws.setTitle(string("sharedFboExample - ") + (i == 1 ? "center" : "right"));
        ws.setQuitOnClose(true);
        auto win = createWindow(ws);
        if (!win) {
            logError("tcApp") << "createWindow failed (single-window platform?)";
            break;
        }
        win->setApp<PanelApp>(wall_, i);
        panels_.push_back(win);
    }
    arrange();
    logNotice("tcApp") << "R: put the windows side by side again";
}

void tcApp::update() {
    t_ += (float)getDeltaTime();

    const float w = (float)wall_->getWidth();
    const float h = (float)wall_->getHeight();

    wall_->begin();
    clear(0.05f, 0.06f, 0.10f);

    // A wave that runs across the whole wall
    const int dots = 72;
    for (int i = 0; i < dots; i++) {
        const float x = w * (i + 0.5f) / dots;
        const float y = h * 0.5f + sinf(x / w * TAU * 2.0f + t_ * TAU * 0.3f) * h * 0.3f;
        setColor(Color::fromHSB((float)i / dots, 0.6f, 1.0f));
        drawCircle(x, y, 7);
    }

    // A ball that travels from window to window
    const float r = 36.0f;
    const float bx = fmodf(t_ * 240.0f, w + r * 2) - r;
    const float by = h * 0.5f + cosf(t_ * TAU * 0.5f) * h * 0.25f;
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
    // Window positions are the window frame's top-left, so leave a small gap
    // for the frames. setWindowPosition() moves THIS window; the others are
    // moved through their handles.
    const int gap = 16;
    const IVec2 origin{80, 120};
    setWindowPosition(origin.x, origin.y);
    for (size_t i = 0; i < panels_.size(); i++) {
        if (!panels_[i]->isOpen()) continue;
        panels_[i]->setPosition(origin.x + (int)(i + 1) * (PANEL_W + gap), origin.y);
    }
}
