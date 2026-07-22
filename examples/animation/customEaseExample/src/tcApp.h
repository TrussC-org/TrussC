#pragma once

#include <TrussC.h>
#include "FreehandCurveNode.h"
#include "PointCurveNode.h"
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void drawTrack(const string& label, float v, float y, Color col);

    shared_ptr<FreehandCurveNode> freehand;
    shared_ptr<PointCurveNode> pointCurve;

    Tween<float> tweenFreehand, tweenPoints, tweenCode;

    // Panels are 1.5x taller than wide: the value range is 1.5 units, so the
    // 0..1 unit box stays square (linear = 45 degrees).
    static constexpr float PANEL_W = 280;
    static constexpr float PANEL_H =
        PANEL_W * (PointCurveNode::VAL_MAX - PointCurveNode::VAL_MIN);
    static constexpr float TRACK_X0 = 700, TRACK_X1 = 940;
};
