// =============================================================================
// tcApp.cpp - Window gallery example
// =============================================================================
// SPACE opens the next window of the gallery. Each one is created with its own
// WindowSettings (decorated, swapInterval or size), plus a Window::setFps()
// target after it is created. Each window shows the fps it measures.
//
// ESC in a gallery window closes that one. Useful as a quick check on a new
// machine: borderless key input, fps per window, and what a minimized window
// does (updates keep counting, draws stop).
// Secondary windows: macOS / Windows / Linux; elsewhere SPACE logs an error.

#include "tcApp.h"

namespace {
struct Preset {
    string label;
    WindowSettings settings;
    float fps;   // Window::setFps() target
};

WindowSettings base() { return WindowSettings().setSize(360, 120); }

const vector<Preset> PRESETS = {
    {"defaults",                  base(),                                   VSYNC},
    {"borderless",                base().setDecorated(false),               VSYNC},
    {"setFps(30)",                base(),                                   30.0f},
    {"setFps(10)",                base(),                                   10.0f},
    {"swapInterval 2",            base().setSwapInterval(2),                VSYNC},
    {"large",                     base().setSize(560, 320),                 VSYNC},
    {"small borderless at 15fps", base().setSize(300, 100).setDecorated(false), 15.0f},
};
} // namespace

void tcApp::draw() {
    clear(0.12f);
    setColor(1.0f);
    drawBitmapString("SPACE: open the next window", 20, 30);
    for (size_t i = 0; i < PRESETS.size(); i++) {
        setColor((int)i == next_ ? Color(1.0f, 0.9f, 0.4f) : Color(0.7f));
        drawBitmapString(toString(i + 1) + ". " + PRESETS[i].label, 20, 60 + 18 * (float)i);
    }
}

void tcApp::keyPressed(int key) {
    if (key != ' ') return;
    const Preset& preset = PRESETS[next_];
    auto win = createWindow(WindowSettings(preset.settings).setTitle(preset.label));
    if (!win) {
        logError("tcApp") << "createWindow failed (single-window platform?)";
        return;
    }
    win->setFps(preset.fps);
    win->setApp<InfoApp>(preset.label, Color::fromHSB(next_ / (float)PRESETS.size(), 0.5f, 0.35f));

    // Cascade the windows to the right of the main window.
    win->setPosition(getWindowPosition().x + getWindowWidth() + 20 + next_ * 30, getWindowPosition().y + next_ * 30);
    next_ = (next_ + 1) % (int)PRESETS.size();
}
