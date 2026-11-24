#pragma once
#include <TrussC.h>
#include <tcxInputReplay.h>
using namespace std;
using namespace tc;
using namespace tcx;

// A point that remembers when it was made, so it can fade out over time.
struct Mark {
    Vec2 pos;
    float time;
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void mouseMoved(Vec2 pos) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mousePressed(Vec2 pos, int button) override;
    shared_ptr<inputreplay::Toolbar> replay;
    deque<Mark> trail;    // mouse path, oldest first
    deque<Mark> clicks;   // click points, oldest first
};
