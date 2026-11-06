// =============================================================================
// tcApp.cpp - Tool palette example
// =============================================================================
// The main window is a canvas: drag to paint. A small palette window, opened
// with WindowSettings::setParentWindow(), offers colors and brush sizes. The
// parent relation only keeps the palette above the canvas. It is not modal:
// the canvas keeps taking input while the palette is open.
//
// What the parent relation does differs a little by OS: only macOS moves the
// palette along with the canvas, Windows hides it while the canvas is minimized,
// and on Linux the window manager decides. See WindowSettings::setParentWindow.
// Secondary windows: macOS / Windows / Linux; elsewhere P logs an error.

#include "tcApp.h"

void tcApp::setup() {
    openPalette();
}

void tcApp::openPalette() {
    if (palette_ && palette_->isOpen()) return;

    // The palette stays above this (the calling App's) window.
    palette_ = createWindow(WindowSettings()
        .setSize(204, 204)
        .setTitle("Palette")
        .setParentWindow(*getWindow()));
    if (!palette_) {
        logError("tcApp") << "createWindow failed (single-window platform?)";
        return;
    }
    palette_->setApp<PaletteApp>(brush_);
    palette_->setPosition(getWindowPosition().x + getWindowWidth() + 16, getWindowPosition().y);
}

void tcApp::draw() {
    clear(0.95f, 0.94f, 0.90f);
    for (const auto& d : dots_) {
        setColor(d.color);
        drawCircle(d.pos, d.radius);
    }
    setColor(0.2f);
    drawBitmapString("drag: paint   C: clear   P: open the palette", 20, 30);
}

void tcApp::keyPressed(int key) {
    if (key == 'P') openPalette();
    if (key == 'C') dots_.clear();
}

void tcApp::mousePressed(Vec2 pos, int button) {
    last_ = pos;
    mouseDragged(pos, button);
}

void tcApp::mouseDragged(Vec2 pos, int) {
    // Dots every half radius since the last position, so a fast drag stays a line.
    const float radius = RADII[brush_->size];
    const int count = max(1, (int)((pos - last_).length() / (radius * 0.5f)));
    for (int i = 1; i <= count; i++) {
        dots_.push_back({last_ + (pos - last_) * ((float)i / count), COLORS[brush_->color], radius});
    }
    last_ = pos;
}
