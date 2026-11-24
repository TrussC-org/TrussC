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
    void mousePressed(Vec2 pos, int button) override;
    shared_ptr<inputreplay::Toolbar> replay;
    vector<Vec2> dots;
};
