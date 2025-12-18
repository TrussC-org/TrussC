#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

// =============================================================================
// textureExample - テクスチャ Filter / Wrap モード比較デモ
// =============================================================================
// 上段: Filter 比較（Nearest / Linear / Cubic）- スライム
// 下段: Wrap 比較（Repeat / ClampToEdge / MirroredRepeat）- レンガ
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // --- Filter 比較用（スライム）---
    Image imgOriginal_;
    Image imgNearest_;
    Image imgLinear_;
    Image imgCubic_;  // CPU でバイキュービック補間済み

    // --- Wrap 比較用（レンガ）---
    Image imgBrickRepeat_;
    Image imgBrickClamp_;
    Image imgBrickMirrored_;

    // パターン生成
    void generatePixelArt(Image& img);   // スライム
    void generateBrickPattern(Image& img);  // レンガ

    // バイキュービック補間でアップスケール
    void upscaleBicubic(const Image& src, Image& dst, int newWidth, int newHeight);
    float cubicWeight(float t);

    // 現在の表示スケール
    float scale_ = 8.0f;  // 3x2 なので少し小さめに
    float lastScale_ = 0.0f;

    // 元画像サイズ
    static constexpr int SRC_SIZE = 16;
    static constexpr int BRICK_SIZE = 8;  // レンガは小さめ（繰り返しを見せるため）
};
