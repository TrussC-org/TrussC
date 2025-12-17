#pragma once

#include "tcBaseApp.h"
#include <iostream>

using namespace std;

// =============================================================================
// textureExample - テクスチャ Filter / Wrap モード比較デモ
// =============================================================================
// 上段: Filter 比較（Nearest / Linear / Cubic）- スライム
// 下段: Wrap 比較（Repeat / ClampToEdge / MirroredRepeat）- レンガ
// =============================================================================
class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // --- Filter 比較用（スライム）---
    tc::Image imgOriginal_;
    tc::Image imgNearest_;
    tc::Image imgLinear_;
    tc::Image imgCubic_;  // CPU でバイキュービック補間済み

    // --- Wrap 比較用（レンガ）---
    tc::Image imgBrickRepeat_;
    tc::Image imgBrickClamp_;
    tc::Image imgBrickMirrored_;

    // パターン生成
    void generatePixelArt(tc::Image& img);   // スライム
    void generateBrickPattern(tc::Image& img);  // レンガ

    // バイキュービック補間でアップスケール
    void upscaleBicubic(const tc::Image& src, tc::Image& dst, int newWidth, int newHeight);
    float cubicWeight(float t);

    // 現在の表示スケール
    float scale_ = 8.0f;  // 3x2 なので少し小さめに
    float lastScale_ = 0.0f;

    // 元画像サイズ
    static constexpr int SRC_SIZE = 16;
    static constexpr int BRICK_SIZE = 8;  // レンガは小さめ（繰り返しを見せるため）
};
