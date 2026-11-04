#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// One gallery window's content. It shows the settings its window was created
// with, the fps target it was given and the fps it actually measures.
// Every window has its own clock, so getFrameRate() / getUpdateCount() /
// getDrawCount() here are THIS window's numbers.
class InfoApp : public App {
public:
    InfoApp(string label, WindowSettings settings, Color bg)
        : label_(std::move(label)), settings_(std::move(settings)), bg_(bg) {}

    void draw() override {
        clear(bg_);

        // getFps() returns VSYNC (negative) for "free-run at vsync".
        const float target = getWindow() ? getWindow()->getFps() : 0.0f;
        const string targetLabel = target > 0.0f ? toString(target) : string("vsync");

        setColor(1.0f);
        drawBitmapString(label_, 12, 20);
        setColor(0.85f);
        drawBitmapString("size " + toString(settings_.width) + "x" + toString(settings_.height)
                         + "   decorated " + (settings_.decorated ? "yes" : "no"), 12, 44);
        drawBitmapString("swapInterval " + toString(settings_.swapInterval)
                         + "   fps target " + targetLabel, 12, 60);
        setColor(1.0f, 0.9f, 0.4f);
        drawBitmapString("measured fps " + toString(getFrameRate(), 1), 12, 84);
        setColor(0.85f);
        // A hidden window keeps updating but stops drawing. Hidden means
        // minimised (and, where the OS reports it, fully covered): minimise
        // this window for a while and watch the two counts.
        drawBitmapString("updates " + toString(getUpdateCount()) + "   draws " + toString(getDrawCount()), 12, 100);
        drawBitmapString("last key " + keyLabel(lastKey_), 12, 116);
        setColor(0.6f);
        drawBitmapString("ESC: close this window", 12, getHeight() - 14);
    }

    void keyPressed(int key) override {
        lastKey_ = key;
        // Borderless windows have no close button, so give every window a key.
        if (key == KEY_ESCAPE) getWindow()->close();
    }

private:
    static string keyLabel(int key) {
        if (key == 0) return "-";
        if (key > 32 && key < 127) return string(1, (char)key);
        return toString(key);
    }

    string label_;
    WindowSettings settings_;
    Color bg_;
    int lastKey_ = 0;
};

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(Vec2 pos, int button) override;

private:
    void openNext();
    void closeAll();

    WindowSettings base_;                  // shared defaults for every gallery window
    vector<shared_ptr<Window>> windows_;   // handles of the windows we opened
    int next_ = 0;                         // index of the next preset
};
