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
    void mousePressed(const MouseEventArgs& e) override;
    void mouseReleased(const MouseEventArgs& e) override;
    void mouseDragged(const MouseDragEventArgs& e) override;
    shared_ptr<inputreplay::Toolbar> replay;
    float radius = 18;
    float wallWidth = 6;
    Vec2 home{240, 300};
    Vec2 ball = home;
    bool dragging = false;
    string status, savedPath;
};
