#pragma once

#include "tcBaseApp.h"
using namespace tc;

using namespace trussc;

// noiseField2dExample - パーリンノイズのデモ
// フローフィールドとノイズテクスチャの可視化

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // 表示モード
    int mode = 0;
    static const int NUM_MODES = 4;

    // アニメーション用
    float time = 0.0f;
    float noiseScale = 0.01f;
    float timeSpeed = 0.5f;

    // パーティクル（フローフィールド用）
    struct Particle {
        float x, y;
        float prevX, prevY;
    };
    std::vector<Particle> particles;

    void drawNoiseTexture();
    void drawFlowField();
    void drawFlowParticles();
    void drawFbmTexture();

    void resetParticles();
};
