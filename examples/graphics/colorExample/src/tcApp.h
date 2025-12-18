#pragma once

#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;

private:
    // デモモード
    int mode_ = 0;
    static constexpr int NUM_MODES = 4;

    // デモ描画
    void drawLerpComparison();     // lerp 方式の比較
    void drawHueWheel();           // 色相環 HSB vs OKLCH
    void drawLightnessDemo();      // 明度均一性デモ
    void drawGradientDemo();       // グラデーション比較
};
