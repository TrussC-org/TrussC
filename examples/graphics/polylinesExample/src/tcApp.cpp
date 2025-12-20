#include "tcApp.h"
#include <sstream>

using std::stringstream;

void tcApp::setup() {
    setWindowTitle("polylinesExample");
    setupPolylines();
}

void tcApp::setupPolylines() {
    int w = getWindowWidth();
    int h = getWindowHeight();
    float cx = w / 2.0f;
    float cy = h / 2.0f;

    // 直線のPolyline
    linePolyline.clear();
    linePolyline.addVertex(50, 100);
    linePolyline.lineTo(150, 150);
    linePolyline.lineTo(100, 200);
    linePolyline.lineTo(200, 200);

    // 3次ベジェ曲線
    bezierPolyline.clear();
    bezierPolyline.addVertex(250, 100);
    bezierPolyline.bezierTo(300, 50, 400, 250, 450, 100);

    // 2次ベジェ曲線
    quadPolyline.clear();
    quadPolyline.addVertex(500, 100);
    quadPolyline.quadBezierTo(600, 250, 700, 100);

    // Catmull-Rom スプライン
    curvePolyline.clear();
    curvePolyline.curveTo(50.0f, 350.0f);   // 制御点1
    curvePolyline.curveTo(100.0f, 300.0f);  // 制御点2（ここから曲線開始）
    curvePolyline.curveTo(200.0f, 400.0f);
    curvePolyline.curveTo(300.0f, 300.0f);
    curvePolyline.curveTo(400.0f, 400.0f);
    curvePolyline.curveTo(450.0f, 350.0f);  // 制御点（ここで曲線終了）

    // 円弧
    arcPolyline.clear();
    arcPolyline.arc(600.0f, 350.0f, 80.0f, 80.0f, 0.0f, 270.0f, 32);

    // 星型（閉じた形状）
    starPolyline.clear();
    int points = 5;
    float outerR = 80;
    float innerR = 35;
    for (int i = 0; i < points * 2; i++) {
        float angle = i * TAU / (points * 2) - QUARTER_TAU;
        float r = (i % 2 == 0) ? outerR : innerR;
        float x = cx + cos(angle) * r;
        float y = cy + 150 + sin(angle) * r;
        starPolyline.addVertex(x, y);
    }
    starPolyline.close();
}

void tcApp::update() {
    time += getDeltaTime();
}

void tcApp::draw() {
    clear(30);

    switch (mode) {
        case 0: drawCurveDemo(); break;
        case 1: drawMouseDrawing(); break;
        case 2: drawAnimatedCurve(); break;
    }

    // UI
    setColor(1.0f);
    stringstream ss;
    ss << "Mode " << (mode + 1) << "/" << NUM_MODES << ": ";
    switch (mode) {
        case 0: ss << "Curve Types Demo"; break;
        case 1: ss << "Mouse Drawing"; break;
        case 2: ss << "Animated Curves"; break;
    }
    ss << "\n\nControls:\n";
    ss << "  1-3: Switch mode\n";
    ss << "  c: Clear mouse drawing";

    drawBitmapString(ss.str(), 20, 20);
}

void tcApp::drawCurveDemo() {
    // ストロークのみで描画（Polylineの塗りつぶしは凸形状のみ対応）
    noFill();
    stroke();

    // 直線
    setColor(colors::red);
    linePolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("lineTo()", 100, 80);

    // 3次ベジェ
    setColor(colors::green);
    bezierPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("bezierTo()", 320, 80);

    // 2次ベジェ
    setColor(colors::blue);
    quadPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("quadBezierTo()", 560, 80);

    // Catmull-Rom
    setColor(colors::orange);
    curvePolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("curveTo()", 200, 280);

    // 円弧
    setColor(colors::blue);
    arcPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("arc()", 570, 280);

    // 星型（閉じた形状）
    setColor(colors::magenta);
    starPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("closed star", getWindowWidth() / 2.0f - 40, getWindowHeight() / 2.0f + 100);

    // デフォルトに戻す
    stroke();
    fill();
}

void tcApp::drawMouseDrawing() {
    setColor(0.78f);
    drawBitmapString("Click and drag to draw a polyline", 20, 120);
    drawBitmapString("Press 'c' to clear", 20, 140);

    // マウス追跡
    if (isMousePressed()) {
        if (!isDrawing) {
            isDrawing = true;
        }
        mousePolyline.addVertex(getMouseX(), getMouseY());
    } else {
        isDrawing = false;
    }

    // 描画
    setColor(colors::lime);
    mousePolyline.draw();

    // 頂点表示
    setColor(colors::red);
    for (size_t i = 0; i < mousePolyline.size(); i++) {
        drawCircle(mousePolyline[i].x, mousePolyline[i].y, 2);
    }

    // 情報表示
    setColor(0.78f);
    stringstream info;
    info << "Vertices: " << mousePolyline.size();
    info << "\nPerimeter: " << (int)mousePolyline.getPerimeter() << " px";
    drawBitmapString(info.str(), 20, 160);
}

void tcApp::drawAnimatedCurve() {
    int w = getWindowWidth();
    int h = getWindowHeight();
    float cx = w / 2.0f;
    float cy = h / 2.0f;

    // アニメーションする花のような形状
    Path flower;
    int petals = 6;
    int segments = 60;

    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * TAU;
        float r = 100 + 50 * sin(petals * angle + time * 2);
        float x = cx + cos(angle) * r;
        float y = cy + sin(angle) * r;
        flower.addVertex(x, y);
    }
    flower.close();

    // グラデーション的な描画
    noFill();
    stroke();
    for (int i = 0; i < 5; i++) {
        float scale = 1.0f - i * 0.15f;
        setColorHSB(time + i * 0.1f, 0.8f, 0.9f);

        Path scaled;
        for (size_t j = 0; j < flower.size(); j++) {
            float x = cx + (flower[j].x - cx) * scale;
            float y = cy + (flower[j].y - cy) * scale;
            scaled.addVertex(x, y);
        }
        scaled.close();
        scaled.draw();
    }

    // リサージュ曲線
    Path lissajous;
    int a = 3, b = 4;
    for (int i = 0; i <= 100; i++) {
        float t = (float)i / 100 * TAU;
        float x = cx + 200 + 80 * sin(a * t + time);
        float y = cy + 80 * sin(b * t);
        lissajous.addVertex(x, y);
    }
    lissajous.close();

    setColor(colors::cyan);
    lissajous.draw();

    setColor(0.78f);
    drawBitmapString("Animated flower & Lissajous curve", 20, 120);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case '1': mode = 0; break;
        case '2': mode = 1; break;
        case '3': mode = 2; break;
        case 'c':
        case 'C':
            mousePolyline.clear();
            break;
    }
}
