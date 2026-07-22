#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// depthTestExample - enableDepthTest() / disableDepthTest()
//
// Blend-mode pipelines draw WITHOUT depth testing (that is what lets 2D
// overlays and additive glows stack in submission order). The flip side:
// after any setBlendMode() call, later 3D geometry no longer depth-sorts
// against the scene. enableDepthTest() restores depth test + write for the
// current blend mode, mid-scene, without starting a new camera scope.
//
// Draw order in this example:
//  1. red box (near) then green box (middle) — the default 3D pipeline
//     depth-tests them, so red occludes green where they overlap
//  2. setBlendMode(Add) + a glow circle — additive, no depth (by design)
//  3. resetBlendMode() + enableDepthTest() + a blue box drawn LAST but
//     placed FARTHEST — with the API it is correctly occluded by the
//     nearer boxes; without it, it would cover them (draw order wins)
//  4. disableDepthTest() + a 2D overlay — draws on top again
class tcApp : public App {
public:
    void update() override;
    void draw() override;

private:
    float time = 0.0f;
};
