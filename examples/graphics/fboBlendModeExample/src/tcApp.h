#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// fboBlendModeExample - setBlendMode() inside an Fbo pass
//
// Blend modes apply inside an Fbo pass exactly as they do on the screen.
// The SAME scene is drawn twice: two half-gray rects overlapping, with
// BlendMode::Add active while the second one is drawn.
//   - Left:  directly to the swapchain
//   - Right: into an Fbo, then the Fbo is composited to the screen
// Both overlap regions read 1.0 (0.5 + 0.5, full white), so the two panes
// are pixel-identical — a visual check that the Fbo pass honors the mode.
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    void drawScene();   // the shared test scene (uses setBlendMode(Add))
    Fbo fbo_;
};
