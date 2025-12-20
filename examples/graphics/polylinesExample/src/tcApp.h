#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

using namespace trussc;

// polylinesExample - Polyline curve feature demo
// Examples of lineTo, bezierTo, quadBezierTo, curveTo, arc

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Polylines for various curves
    Path linePolyline;      // Line
    Path bezierPolyline;    // Cubic Bezier
    Path quadPolyline;      // Quadratic Bezier
    Path curvePolyline;     // Catmull-Rom spline
    Path arcPolyline;       // Arc
    Path starPolyline;      // Star (closed shape)

    // Polyline drawn with mouse
    Path mousePolyline;
    bool isDrawing = false;

    // Display mode
    int mode = 0;
    static const int NUM_MODES = 3;

    void setupPolylines();
    void drawCurveDemo();
    void drawMouseDrawing();
    void drawAnimatedCurve();

    float time = 0;
};
