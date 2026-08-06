#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// curvesExample
//
// Showcase for the new curve primitives and CurveStyle:
//
//   - drawArc      (radians, sector fill / stroke)
//   - drawBezier   (cubic, quadratic, n-th order)
//   - drawCurve    (Catmull-Rom)
//
// Plus the tolerance-vs-resolution comparison: same circle drawn at
// several `setCurveTolerance` values on the left, and several
// `setCurveResolution` values on the right. Increase tolerance with
// the keyboard to watch every curve in the scene get chunkier.
//
// Keys:
//   T / Shift+T   global tolerance --/++  (active in tolerance mode)
//   R / Shift+R   global resolution --/++ (active in resolution mode)
//   M             toggle tolerance/resolution mode
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    // Takes the KeyEventArgs overload because the controls are shift-modified:
    // keyPressed(int) receives a raw sapp_keycode, whose letter values are
    // uppercase ASCII whether or not shift is held, so shift is unreadable
    // from the int form.
    void keyPressed(const KeyEventArgs& e) override;

private:
    bool tolMode_ = true;
    float tolerance_ = 0.5f;
    int   resolution_ = 24;

    void applyMode();
    void drawHud();
};
