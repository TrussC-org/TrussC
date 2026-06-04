#pragma once

#include <TrussC.h>
#include <tcxDepthCamera.h>
using namespace std;
using namespace tc;
using namespace tcx;

// Point-cloud viewer driven by SyntheticDepthCamera - no hardware required.
// Demonstrates the tcxDepthCamera interface: update() -> isFrameNew() -> toMesh().
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void rebuild();

    shared_ptr<SyntheticDepthCamera> camera;
    EasyCam view;
    Mesh cloud;
    bool colored = false;   // uniform color by default (clearer shape); C toggles depth-keyed
    int step = 1;
    bool repeatDepth = false;   // R toggles the depth preview's repeating band
};
