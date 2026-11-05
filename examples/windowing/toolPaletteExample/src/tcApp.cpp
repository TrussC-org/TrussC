// =============================================================================
// tcApp.cpp - Tool palette example
// =============================================================================
// The main window is a canvas: drag to paint. A small palette window, opened
// with WindowSettings::setParentWindow(), offers colors and brush sizes; click
// to choose. The parent relation only keeps the palette above the canvas. It is
// not modal: the canvas keeps taking input while the palette is open, and it
// still works after the palette is closed.
//
// Keys on the canvas: P opens the palette again (if it was closed), C clears.
// Closing the canvas quits; closing the palette does not.
//
// What the parent relation does differs a little by OS: only macOS moves the
// palette along with the canvas, Windows hides it while the canvas is minimised,
// and on Linux the window manager decides. See WindowSettings::setParentWindow.
// Secondary windows: macOS / Windows / Linux; elsewhere P logs an error.

#include "tcApp.h"

void tcApp::setup() {
    openPalette();
    logNotice("tcApp") << "Drag: paint   P: open the palette   C: clear";
}

void tcApp::openPalette() {
    if (palette_ && palette_->isOpen()) {
        palette_->bringToFront();
        return;
    }

    WindowSettings ws;
    ws.setSize(200, 220);
    ws.setTitle("Palette");
    // The palette stays above this (the calling App's) window.
    ws.setParentWindow(*getWindow());
    palette_ = createWindow(ws);
    if (!palette_) {
        logError("tcApp") << "createWindow failed (single-window platform?)";
        return;
    }
    palette_->setApp<PaletteApp>(brush_);

    // Beside the canvas, near its top-right corner.
    const IVec2 p = getWindowPosition();
    palette_->setPosition(p.x + getWindowWidth() + 16, p.y);
}

void tcApp::paint(Vec2 pos) {
    const float radius = brushRadii()[brush_->size];
    const Color color = swatchColors()[brush_->color];
    // Fill the gap since the last position so a fast drag stays a line.
    const float step = radius * 0.5f;
    const float distance = (pos - last_).length();
    const int count = painting_ ? max(1, (int)(distance / step)) : 1;
    for (int i = 1; i <= count; i++) {
        const float t = (float)i / count;
        dots_.push_back({painting_ ? last_ + (pos - last_) * t : pos, color, radius});
    }
    last_ = pos;
    painting_ = true;
}

void tcApp::draw() {
    clear(0.95f, 0.94f, 0.90f);

    for (const auto& d : dots_) {
        setColor(d.color);
        drawCircle(d.pos.x, d.pos.y, d.radius);
    }

    setColor(0.2f);
    drawBitmapString("CANVAS   drag: paint   C: clear   P: open the palette", 20, 30);
    if (!palette_ || !palette_->isOpen()) {
        setColor(0.8f, 0.2f, 0.2f);
        drawBitmapString("palette closed: press P", 20, 50);
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'P') openPalette();
    if (key == 'C') dots_.clear();
}

void tcApp::mousePressed(Vec2 pos, int button) {
    if (button != MOUSE_BUTTON_LEFT) return;
    painting_ = false;
    paint(pos);
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    if (button == MOUSE_BUTTON_LEFT) paint(pos);
}

void tcApp::mouseReleased(Vec2, int) {
    painting_ = false;
}
