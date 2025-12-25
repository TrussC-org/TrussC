#pragma once

#include <TrussC.h>
#include <iostream>
using namespace std;
using namespace tc;

// =============================================================================
// textureExample - Texture Filter / Wrap mode comparison demo
// =============================================================================
// Top row: Filter comparison (Nearest / Linear / Cubic) - Slime
// Bottom row: Wrap comparison (Repeat / ClampToEdge / MirroredRepeat) - Brick
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // --- For Filter comparison (Slime) ---
    Image imgOriginal_;
    Image imgNearest_;
    Image imgLinear_;
    Image imgCubic_;  // Bicubic interpolated on CPU

    // --- For Wrap comparison (Brick) ---
    Image imgBrickRepeat_;
    Image imgBrickClamp_;
    Image imgBrickMirrored_;

    // Pattern generation
    void generatePixelArt(Image& img);   // Slime
    void generateBrickPattern(Image& img);  // Brick

    // Upscale with bicubic interpolation
    void upscaleBicubic(const Image& src, Image& dst, int newWidth, int newHeight);
    float cubicWeight(float t);

    // Current display scale
    float scale_ = 8.0f;  // Slightly smaller for 3x2 layout
    float lastScale_ = 0.0f;

    // Original image size
    static constexpr int SRC_SIZE = 16;
    static constexpr int BRICK_SIZE = 8;  // Brick is smaller (to show repetition)
};
