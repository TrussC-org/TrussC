#include "tcApp.h"
#include <cmath>

// ---------------------------------------------------------------------------
// setup - 初期化
// ---------------------------------------------------------------------------
void tcApp::setup() {
    sourcePixels_.resize(AnalysisThread::TOTAL_PIXELS);
}

// ---------------------------------------------------------------------------
// update - 更新
// ---------------------------------------------------------------------------
void tcApp::update() {
    frameNum_++;

    // パターンを生成（sin波 + ノイズ）
    float t = frameNum_ * 0.05f;
    for (int i = 0; i < AnalysisThread::TOTAL_PIXELS; i++) {
        float ux = (i % AnalysisThread::WIDTH) / (float)AnalysisThread::WIDTH;
        float uy = (i / AnalysisThread::WIDTH) / (float)AnalysisThread::HEIGHT;

        // 波模様パターン
        float value = std::sin(ux * 10.0f + t) * std::sin(uy * 8.0f + t * 0.7f);
        value = (value + 1.0f) * 0.5f;  // 0-1 に正規化
        sourcePixels_[i] = value;
    }

    // 解析リクエストを送信
    analyzer_.analyze(sourcePixels_);

    // 解析結果を受信
    analyzer_.update();
}

// ---------------------------------------------------------------------------
// draw - 描画
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.1f);

    // 元のパターンを描画（左側）
    setColor(255);
    drawBitmapString("Source (Main Thread)", 20, 20);

    for (int py = 0; py < AnalysisThread::HEIGHT; py++) {
        for (int px = 0; px < AnalysisThread::WIDTH; px++) {
            int i = py * AnalysisThread::WIDTH + px;
            float value = sourcePixels_[i];
            setColor(value, value, value);
            drawRect(20 + px * 4, 40 + py * 4, 4, 4);
        }
    }

    // 解析結果を描画（右側）
    setColor(255);
    drawBitmapString("Analyzed (Worker Thread)", 300, 20);
    analyzer_.draw(300, 40);

    // 情報表示
    setColor(200, 200, 200);
    drawBitmapString("Frame: " + toString(frameNum_), 20, 260);
    drawBitmapString("Analyzed: " + toString(analyzer_.getAnalyzedCount()), 20, 275);

    setColor(100, 200, 100);
    drawBitmapString("ThreadChannel Demo:", 20, 310);
    drawBitmapString("  Main -> Worker: source pixels", 20, 325);
    drawBitmapString("  Worker -> Main: thresholded result", 20, 340);
}
