#include "tcApp.h"
#include <iostream>

using namespace std;

// ---------------------------------------------------------------------------
// setup - 初期化
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "setup() called" << endl;
}

// ---------------------------------------------------------------------------
// update - 更新
// ---------------------------------------------------------------------------
void tcApp::update() {
    // ロジック更新があればここに
}

// ---------------------------------------------------------------------------
// draw - 描画
// ---------------------------------------------------------------------------
void tcApp::draw() {
    double t = getElapsedTime();

    // 背景クリア
    clear(0.15f, 0.15f, 0.2f);

    // ----------------------
    // 四角形
    // ----------------------
    setColor(0.9f, 0.3f, 0.3f);
    drawRect(50, 50, 150, 100);

    // ストローク付きの四角形
    noFill();
    stroke();
    setColor(1.0f, 1.0f, 0.3f);
    drawRect(50, 180, 150, 100);
    fill();
    noStroke();

    // ----------------------
    // 円
    // ----------------------
    // 大きい円は解像度を上げて滑らかに
    setCircleResolution(100);
    setColor(0.3f, 0.9f, 0.3f);
    drawCircle(350, 100, 60);
    setCircleResolution(20);  // デフォルトに戻す

    // アニメーションする円（デフォルト解像度=20角形）
    float pulse = (float)(sin(t * 3.0) * 0.3 + 0.7);
    setColor(0.3f, 0.7f, 0.9f, pulse);
    drawCircle(350, 250, 50);

    // ----------------------
    // 楕円
    // ----------------------
    setColor(0.9f, 0.5f, 0.9f);
    drawEllipse(550, 100, 80, 50);

    // ----------------------
    // 線
    // ----------------------
    setColor(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 10; i++) {
        float angle = (float)i / 10.0f * TAU + (float)t;
        float x2 = 550 + cos(angle) * 80;
        float y2 = 250 + sin(angle) * 80;
        drawLine(550, 250, x2, y2);
    }

    // ----------------------
    // 三角形
    // ----------------------
    setColor(0.9f, 0.6f, 0.2f);
    drawTriangle(750, 50, 850, 150, 650, 150);

    // 回転する三角形
    pushMatrix();
    translate(750, 250);
    rotate((float)t);
    setColor(0.5f, 0.9f, 0.9f);
    drawTriangle(-50, -30, 50, -30, 0, 50);
    popMatrix();

    // ----------------------
    // カスタムシェイプ（beginShape/endShape）
    // ----------------------
    // 五角形（fill）
    setColor(0.8f, 0.4f, 0.8f);
    beginShape();
    for (int i = 0; i < 5; i++) {
        float angle = TAU * i / 5.0f - HALF_TAU / 2.0f;
        vertex(150 + cos(angle) * 50, 450 + sin(angle) * 50);
    }
    endShape(true);

    // 星型（stroke）
    noFill();
    stroke();
    setColor(1.0f, 0.9f, 0.2f);
    beginShape();
    for (int i = 0; i < 10; i++) {
        float angle = TAU * i / 10.0f - HALF_TAU / 2.0f;
        float r = (i % 2 == 0) ? 60.0f : 30.0f;
        vertex(350 + cos(angle) * r, 450 + sin(angle) * r);
    }
    endShape(true);
    fill();
    noStroke();

    // アニメーションするカスタムシェイプ
    setColor(0.3f, 0.8f, 0.9f, 0.8f);
    beginShape();
    int numPoints = 6;
    for (int i = 0; i < numPoints; i++) {
        float angle = TAU * i / numPoints + (float)t;
        float r = 40 + sin(t * 2 + i) * 20;
        vertex(550 + cos(angle) * r, 450 + sin(angle) * r);
    }
    endShape(true);

    // Path で波形（メンバ変数、100頂点）
    noFill();
    stroke();
    setColor(0.2f, 1.0f, 0.6f);
    wave.clear();
    for (int i = 0; i < 100; i++) {
        float x = 650 + i * 2;
        float y = 450 + sin(i * 0.1f + t * 3) * 30;
        wave.addVertex(x, y);
    }
    wave.draw();
    fill();
    noStroke();

    // ----------------------
    // Mesh（頂点カラー付き三角形）
    // ----------------------
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);
    // 3頂点
    mesh.addVertex(750, 530);
    mesh.addVertex(850, 650);
    mesh.addVertex(650, 650);
    // 頂点カラー（RGB）
    mesh.addColor(1.0f, 0.0f, 0.0f);  // 赤
    mesh.addColor(0.0f, 1.0f, 0.0f);  // 緑
    mesh.addColor(0.0f, 0.0f, 1.0f);  // 青
    mesh.draw();

    // ----------------------
    // グリッド描画
    // ----------------------
    setColor(0.6f, 0.6f, 0.6f, 0.5f);
    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 2; y++) {
            float px = 100.0f + x * 80.0f;
            float py = 550.0f + y * 80.0f;
            float size = 20.0f + (float)sin(t * 2.0 + x * 0.5 + y * 0.3) * 10.0f;
            drawCircle(px, py, size);
        }
    }

    // ----------------------
    // マウス位置に円を描画（getGlobalMouseX/Y を使用）
    // ----------------------
    setColor(1.0f, 0.3f, 0.5f, 0.8f);
    drawCircle(getGlobalMouseX(), getGlobalMouseY(), 20);

    // マウスが押されていたら色を変える
    if (isMousePressed()) {
        setColor(0.3f, 1.0f, 0.5f, 0.8f);
        drawCircle(getGlobalMouseX(), getGlobalMouseY(), 30);
    }

    // FPS表示
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("FPS: " + toString(getFrameRate(), 1), 10, 20);
}

// ---------------------------------------------------------------------------
// 入力イベント
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    cout << "keyPressed: " << key << endl;

    // ESC で終了
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    cout << "mousePressed: " << x << ", " << y << " button=" << button << endl;
}

void tcApp::mouseDragged(int x, int y, int button) {
    (void)x; (void)y; (void)button;
    // マウス位置は getMouseX/Y で取得できるので、ここでは何もしない
}
