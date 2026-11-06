#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// One gallery window's content: its label, the fps it measures, and how often
// it updated and drew. Every window has its own clock, so getFrameRate(),
// getUpdateCount() and getDrawCount() here are THIS window's numbers.
class InfoApp : public App {
public:
    InfoApp(string label, Color bg) : label_(std::move(label)), bg_(bg) {}

    void draw() override {
        clear(bg_);
        setColor(1.0f);
        drawBitmapString(label_, 12, 20);
        drawBitmapString("measured fps " + toString(getFrameRate(), 1), 12, 44);
        // A hidden (minimized) window keeps updating but stops drawing.
        drawBitmapString("updates " + toString(getUpdateCount()) + "   draws " + toString(getDrawCount()), 12, 60);
        drawBitmapString("ESC: close this window", 12, 84);
    }

    void keyPressed(int key) override {
        // Borderless windows have no close button, so give every window a key.
        if (key == KEY_ESCAPE) getWindow()->close();
    }

private:
    string label_;
    Color bg_;
};

class tcApp : public App {
public:
    void draw() override;
    void keyPressed(int key) override;

private:
    int next_ = 0;   // index of the next preset
};
