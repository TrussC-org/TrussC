// =============================================================================
// customEaseExample - Custom easing curves via Tween::ease(EaseFunction)
//
// Three ways to feed a custom curve into a Tween:
// - FreehandCurveNode: draw the curve directly with the mouse (LUT)
// - PointCurveNode: tone-curve style editor with draggable control points
// - a plain code-defined lambda (no editor at all)
//
// Each editor is captured BY REFERENCE in the EaseFunction passed to ease(),
// so editing a curve changes the ball's motion immediately — the function is
// registered once and never re-set.
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Custom Ease Example");

    freehand = make_shared<FreehandCurveNode>();
    freehand->setPos(40, 90);
    freehand->setSize(PANEL_W, PANEL_H);
    addChild(freehand);

    pointCurve = make_shared<PointCurveNode>();
    pointCurve->setPos(370, 90);
    pointCurve->setSize(PANEL_W, PANEL_H);
    addChild(pointCurve);

    tweenFreehand.from(0).to(1).duration(1.6f)
        .ease([this](float t) { return freehand->eval(t); })
        .loop(-1).yoyo()
        .start();

    tweenPoints.from(0).to(1).duration(1.6f)
        .ease([this](float t) { return pointCurve->eval(t); })
        .loop(-1).yoyo()
        .start();

    // No editor at all — one lambda is a complete custom ease
    tweenCode.from(0).to(1).duration(1.6f)
        .ease([](float t) { return t * t * (3.0f - 2.0f * t); })  // smoothstep
        .loop(-1).yoyo()
        .start();
}

void tcApp::draw() {
    clear(0.15f, 0.15f, 0.18f);

    setColor(1.0f);
    drawBitmapString("Freehand (drag to draw)", 40, 68);
    drawBitmapString("Points (drag the dots)", 370, 68);
    drawBitmapString("Tween::ease(fn) - curves are captured by reference, edits apply live",
                     40, 30);

    drawTrack("freehand", tweenFreehand.getValue(), 160, Color(0.3f, 0.85f, 0.55f));
    drawTrack("points",   tweenPoints.getValue(),   300, Color(1.0f, 0.85f, 0.3f));
    drawTrack("code lambda (smoothstep)", tweenCode.getValue(), 440,
              Color(0.4f, 0.6f, 1.0f));
}

void tcApp::drawTrack(const string& label, float v, float y, Color col) {
    setColor(1.0f);
    drawBitmapString(label, TRACK_X0, y - 24);
    setColor(0.35f);
    drawLine(TRACK_X0, y, TRACK_X1, y);
    setColor(col);
    // v may overshoot outside 0..1 — the ball simply passes the track end
    drawCircle(TRACK_X0 + (TRACK_X1 - TRACK_X0) * v, y, 12);
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) sapp_request_quit();
}
