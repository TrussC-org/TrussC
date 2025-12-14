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
    double t = tc::getElapsedTime();

    // 背景クリア
    tc::clear(0.15f, 0.15f, 0.2f);

    // ----------------------
    // 四角形
    // ----------------------
    tc::setColor(0.9f, 0.3f, 0.3f);
    tc::drawRect(50, 50, 150, 100);

    // ストローク付きの四角形
    tc::noFill();
    tc::stroke();
    tc::setColor(1.0f, 1.0f, 0.3f);
    tc::drawRect(50, 180, 150, 100);
    tc::fill();
    tc::noStroke();

    // ----------------------
    // 円
    // ----------------------
    tc::setColor(0.3f, 0.9f, 0.3f);
    tc::drawCircle(350, 100, 60);

    // アニメーションする円
    float pulse = (float)(sin(t * 3.0) * 0.3 + 0.7);
    tc::setColor(0.3f, 0.7f, 0.9f, pulse);
    tc::drawCircle(350, 250, 50);

    // ----------------------
    // 楕円
    // ----------------------
    tc::setColor(0.9f, 0.5f, 0.9f);
    tc::drawEllipse(550, 100, 80, 50);

    // ----------------------
    // 線
    // ----------------------
    tc::setColor(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 10; i++) {
        float angle = (float)i / 10.0f * tc::TWO_PI + (float)t;
        float x2 = 550 + cos(angle) * 80;
        float y2 = 250 + sin(angle) * 80;
        tc::drawLine(550, 250, x2, y2);
    }

    // ----------------------
    // 三角形
    // ----------------------
    tc::setColor(0.9f, 0.6f, 0.2f);
    tc::drawTriangle(750, 50, 850, 150, 650, 150);

    // 回転する三角形
    tc::pushMatrix();
    tc::translate(750, 250);
    tc::rotate((float)t);
    tc::setColor(0.5f, 0.9f, 0.9f);
    tc::drawTriangle(-50, -30, 50, -30, 0, 50);
    tc::popMatrix();

    // ----------------------
    // グリッド描画
    // ----------------------
    tc::setColor(0.6f, 0.6f, 0.6f, 0.5f);
    for (int x = 0; x < 10; x++) {
        for (int y = 0; y < 4; y++) {
            float px = 100.0f + x * 80.0f;
            float py = 400.0f + y * 80.0f;
            float size = 20.0f + (float)sin(t * 2.0 + x * 0.5 + y * 0.3) * 10.0f;
            tc::drawCircle(px, py, size);
        }
    }

    // ----------------------
    // マウス位置に円を描画（tc::getGlobalMouseX/Y を使用）
    // ----------------------
    tc::setColor(1.0f, 0.3f, 0.5f, 0.8f);
    tc::drawCircle(tc::getGlobalMouseX(), tc::getGlobalMouseY(), 20);

    // マウスが押されていたら色を変える
    if (tc::isMousePressed()) {
        tc::setColor(0.3f, 1.0f, 0.5f, 0.8f);
        tc::drawCircle(tc::getGlobalMouseX(), tc::getGlobalMouseY(), 30);
    }
}

// ---------------------------------------------------------------------------
// 入力イベント
// ---------------------------------------------------------------------------

void tcApp::keyPressed(int key) {
    cout << "keyPressed: " << key << endl;

    // ESC で終了
    if (key == tc::KEY_ESCAPE) {
        sapp_request_quit();
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    cout << "mousePressed: " << x << ", " << y << " button=" << button << endl;
}

void tcApp::mouseDragged(int x, int y, int button) {
    (void)x; (void)y; (void)button;
    // マウス位置は tc::getMouseX/Y で取得できるので、ここでは何もしない
}
