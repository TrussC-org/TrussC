// =============================================================================
// tcApp.cpp - Window gallery example
// =============================================================================
// SPACE (or a click) opens the next window of the gallery. Each one starts
// from the same WindowSettings (base_) and changes only a few fields:
// decorated, swapInterval or size, plus a Window::setFps() target after it
// is created. Settings belong to each window; the order you open them in
// does not matter. Each window shows its settings and the fps it measures.
//
// C closes every gallery window; ESC in a gallery window closes that one.
// Useful as a quick check on a new machine: borderless key input, fps per
// window, and what a minimised window does (updates keep counting, draws stop).
// Secondary windows: macOS / Windows / Linux; elsewhere SPACE logs an error.

#include "tcApp.h"

namespace {
struct Preset {
    string label;
    function<void(WindowSettings&)> change;   // what differs from the shared defaults
    float fps;                                // Window::setFps() target
};

const vector<Preset>& presets() {
    static const vector<Preset> list = {
        {"defaults",                  [](WindowSettings&) {},                           VSYNC},
        {"borderless",                [](WindowSettings& s) { s.setDecorated(false); },  VSYNC},
        {"setFps(30)",                [](WindowSettings&) {},                           30.0f},
        {"setFps(10)",                [](WindowSettings&) {},                           10.0f},
        {"swapInterval 0 (no vsync)", [](WindowSettings& s) { s.setSwapInterval(0); }, VSYNC},
        {"swapInterval 2",            [](WindowSettings& s) { s.setSwapInterval(2); }, VSYNC},
        {"large",                     [](WindowSettings& s) { s.setSize(560, 320); },   VSYNC},
        {"small borderless at 15fps", [](WindowSettings& s) { s.setSize(300, 150).setDecorated(false); }, 15.0f},
    };
    return list;
}
} // namespace

void tcApp::setup() {
    // Shared defaults: every gallery window starts from a copy of these.
    base_.setSize(360, 200);
    logNotice("tcApp") << "SPACE / click: open the next window   C: close all";
}

void tcApp::draw() {
    clear(0.12f);

    setColor(1.0f);
    drawBitmapString("MAIN window   SPACE / click: open next   C: close all", 20, 30);

    int open = 0;
    for (auto& w : windows_) if (w->isOpen()) open++;
    drawBitmapString("open gallery windows: " + toString(open), 20, 50);
    setColor(0.6f);
    drawBitmapString("pixelPerfect is process-wide (set in main.cpp), not per window", 20, 66);

    const auto& list = presets();
    for (size_t i = 0; i < list.size(); i++) {
        const bool isNext = (int)i == next_;
        setColor(isNext ? Color(1.0f, 0.9f, 0.4f) : Color(0.7f));
        drawBitmapString((isNext ? "> " : "  ") + toString(i + 1) + ". " + list[i].label,
                         20, 90 + 18 * (float)i);
    }
}

void tcApp::keyPressed(int key) {
    if (key == ' ') openNext();
    if (key == 'C') closeAll();
}

void tcApp::mousePressed(Vec2 pos, int button) {
    (void)pos;
    if (button == MOUSE_BUTTON_LEFT) openNext();
}

void tcApp::openNext() {
    const auto& list = presets();
    const Preset& preset = list[next_];
    const int index = next_;
    next_ = (next_ + 1) % (int)list.size();

    // Copy the shared defaults, then change only what this window needs.
    WindowSettings ws = base_;
    preset.change(ws);
    ws.setTitle("gallery " + toString(index + 1) + ": " + preset.label);

    auto win = createWindow(ws);
    if (!win) {
        logError("tcApp") << "createWindow failed (single-window platform?)";
        return;
    }
    win->setFps(preset.fps);
    win->setApp<InfoApp>(preset.label, ws, Color::fromHSB(index / (float)list.size(), 0.5f, 0.35f));

    // Forget handles of windows that were closed meanwhile.
    windows_.erase(remove_if(windows_.begin(), windows_.end(),
                             [](const shared_ptr<Window>& w) { return !w->isOpen(); }),
                   windows_.end());

    // Cascade the windows to the right of the main window.
    const IVec2 p = getWindowPosition();
    const int step = (int)windows_.size() % 8;
    win->setPosition(p.x + getWindowWidth() + 20 + step * 30, p.y + step * 30);

    windows_.push_back(win);
}

void tcApp::closeAll() {
    for (auto& w : windows_) w->close();
    windows_.clear();
}
