#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// fboBlendModeExample - setBlendMode() inside an Fbo pass
//
// The SAME scene is drawn twice: two half-gray rects overlapping, with
// BlendMode::Add active while the second one is drawn.
//   - Left:  directly to the swapchain
//   - Right: into an Fbo, then the Fbo is composited to the screen
// If blend modes work inside Fbo passes, both overlap regions read 1.0
// (0.5 + 0.5, full white). If setBlendMode is ignored in the Fbo, the right
// overlap stays 0.5 — the visual proof of the bug this example pins down.
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    void drawScene();   // the shared test scene (uses setBlendMode(Add))
    Fbo fbo_;
};
