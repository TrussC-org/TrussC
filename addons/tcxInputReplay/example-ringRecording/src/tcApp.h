#pragma once
#include <TrussC.h>
#include <tcxInputReplay.h>
using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(const MouseEventArgs& e) override {
        // Only a ball at home can be grabbed, so every drag starts from home.
        dragging = ball == home && !replay->isPointerOver() && (e.pos - home).length() < 18;
    }
    void mouseReleased(const MouseEventArgs& e) override {
        if (dragging) ball = home;
        dragging = false;
    }
    void mouseDragged(const MouseDragEventArgs& e) override;
    shared_ptr<inputreplay::Toolbar> replay;
    Vec2 home{240, 300};
    Vec2 ball = home;
    bool dragging = false;
    string status, savedPath;
};
