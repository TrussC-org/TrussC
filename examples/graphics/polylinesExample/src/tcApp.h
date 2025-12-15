#pragma once

#include "tcBaseApp.h"

using namespace trussc;

// polylinesExample - Polylineの曲線機能デモ
// lineTo, bezierTo, quadBezierTo, curveTo, arc の使用例

class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // 各種曲線のPolyline
    Polyline linePolyline;      // 直線
    Polyline bezierPolyline;    // 3次ベジェ
    Polyline quadPolyline;      // 2次ベジェ
    Polyline curvePolyline;     // Catmull-Rom スプライン
    Polyline arcPolyline;       // 円弧
    Polyline starPolyline;      // 星型（閉じた形状）

    // マウスで描画するPolyline
    Polyline mousePolyline;
    bool isDrawing = false;

    // 表示モード
    int mode = 0;
    static const int NUM_MODES = 3;

    void setupPolylines();
    void drawCurveDemo();
    void drawMouseDrawing();
    void drawAnimatedCurve();

    float time = 0;
};
