#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// imageOpsExample
//
// Demonstrates the new Image / Pixels operations:
//   - halve()                  : 1/2 box-averaged downscale (gamma-correct)
//   - resize(newW, newH)       : arbitrary resampling, quality first
//                                (BoxArea on downscale, Catmull-Rom bicubic
//                                on upscale; gamma-correct for U8)
//   - crop(x, y, w, h)         : sub-region with clamp-to-edge outside
//   - mirror(h, v) / H() / V() : in-place flips
//
// One procedural source image is built in setup() and several derived
// images apply each operation. Each derived image is drawn into a labelled
// cell so the effect is visible at a glance.

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    // Procedural source: 16x16 checker + central radial highlight + a red
    // marker in the bottom-left so mirror operations are unambiguous.
    void buildPattern(Image& img, int w, int h);

    Image source_;
    Image halve1_;
    Image halve3_;
    Image resizeUp_;
    Image resizeDown_;
    Image resizeAniso_;
    Image cropInside_;
    Image cropClamp_;
    Image mirrorH_;
    Image mirrorV_;
    Image mirrorBoth_;
    bool updatedOnce_ = false;
};
