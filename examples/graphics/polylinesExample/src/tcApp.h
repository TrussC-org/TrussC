#pragma once

#include "tcBaseApp.h"
using namespace tc;

using namespace trussc;

// polylinesExample - Polylineの曲線機能デモ
// lineTo, bezierTo, quadBezierTo, curveTo, arc の使用例

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // 各種曲線のPolyline
    Path linePolyline;      // 直線
    Path bezierPolyline;    // 3次ベジェ
    Path quadPolyline;      // 2次ベジェ
    Path curvePolyline;     // Catmull-Rom スプライン
    Path arcPolyline;       // 円弧
    Path starPolyline;      // 星型（閉じた形状）

    // マウスで描画するPolyline
    Path mousePolyline;
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
