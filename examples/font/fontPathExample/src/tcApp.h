#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// tcApp — vector-path glyph demo
//
// Showcases Font::getStringPath / getGlyphPath. Unlike drawString (atlas
// quads), Path output stays crisp under arbitrary transforms and can be
// stroked, filled, animated, or fed into Path utilities (StrokeMesh,
// CurveTessellation, etc.).
//
// Panels:
//   1. Static stroke at large size — same string vs. atlas drawString
//   2. Animated rotate + pulse-scale around screen center
//   3. Tategaki (vertical) routed through the same getStringPath
//   4. Per-glyph offset wiggle (still using getStringPath)
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    Font fontDemo;    // large font for path emit (size only affects scale)
    Font fontV;       // same font in vertical mode
    Font fontLabel;   // small label font
    float spin_ = 0;  // animated rotation in radians
};
