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
    void mousePressed(const MouseEventArgs& e) override { dragging = !replay->isPointerOver() && (e.pos - ball).length() < 18; }
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
