// =============================================================================
// textureExample - テクスチャ Filter / Wrap モード比較デモ
// =============================================================================
// 上段: Filter 比較（Nearest / Linear / Cubic）- スライム
// 下段: Wrap 比較（Repeat / ClampToEdge / MirroredRepeat）- レンガ
// =============================================================================

#include "tcApp.h"
#include <cmath>
#include <algorithm>

void tcApp::setup() {
    cout << "=== textureExample ===" << endl;
    cout << "Texture Filter & Wrap Mode Demo" << endl;
    cout << "[UP/DOWN] Change scale" << endl;
    cout << "[1] Scale 4x  [2] Scale 8x  [3] Scale 16x  [4] Scale 32x" << endl;

    // --- Filter 比較用（スライム）---
    imgOriginal_.allocate(SRC_SIZE, SRC_SIZE, 4);
    generatePixelArt(imgOriginal_);
    imgOriginal_.update();

    imgNearest_.allocate(SRC_SIZE, SRC_SIZE, 4);
    generatePixelArt(imgNearest_);
    imgNearest_.setFilter(tc::TextureFilter::Nearest);
    imgNearest_.update();

    imgLinear_.allocate(SRC_SIZE, SRC_SIZE, 4);
    generatePixelArt(imgLinear_);
    imgLinear_.setFilter(tc::TextureFilter::Linear);
    imgLinear_.update();

    // Cubic 用は update() で生成

    // --- Wrap 比較用（レンガ）---
    imgBrickRepeat_.allocate(BRICK_SIZE, BRICK_SIZE, 4);
    generateBrickPattern(imgBrickRepeat_);
    imgBrickRepeat_.setFilter(tc::TextureFilter::Nearest);
    imgBrickRepeat_.setWrap(tc::TextureWrap::Repeat);
    imgBrickRepeat_.update();

    imgBrickClamp_.allocate(BRICK_SIZE, BRICK_SIZE, 4);
    generateBrickPattern(imgBrickClamp_);
    imgBrickClamp_.setFilter(tc::TextureFilter::Nearest);
    imgBrickClamp_.setWrap(tc::TextureWrap::ClampToEdge);
    imgBrickClamp_.update();

    imgBrickMirrored_.allocate(BRICK_SIZE, BRICK_SIZE, 4);
    generateBrickPattern(imgBrickMirrored_);
    imgBrickMirrored_.setFilter(tc::TextureFilter::Nearest);
    imgBrickMirrored_.setWrap(tc::TextureWrap::MirroredRepeat);
    imgBrickMirrored_.update();
}

void tcApp::update() {
    // スケールが変わったら Cubic 画像を再生成
    if (scale_ != lastScale_) {
        int newSize = (int)(SRC_SIZE * scale_);
        upscaleBicubic(imgOriginal_, imgCubic_, newSize, newSize);
        imgCubic_.setFilter(tc::TextureFilter::Nearest);
        imgCubic_.update();
        lastScale_ = scale_;
    }
}

void tcApp::draw() {
    tc::clear(0.15f, 0.15f, 0.18f);

    float w = tc::getWindowWidth();
    float h = tc::getWindowHeight();

    // タイトル
    tc::setColor(1.0f, 1.0f, 1.0f);
    tc::drawBitmapString("Texture Filter & Wrap Mode Demo", 20, 25);
    tc::setColor(0.6f, 0.6f, 0.6f);
    tc::drawBitmapString("Scale: " + to_string((int)scale_) + "x  [UP/DOWN or 1-4]", 20, 42);

    // レイアウト計算
    float margin = 15;
    float headerHeight = 55;
    float labelHeight = 20;
    float availWidth = w - margin * 4;
    float availHeight = h - headerHeight - margin * 3 - labelHeight * 2;
    float colWidth = availWidth / 3;
    float rowHeight = availHeight / 2;

    float imgSize = min(colWidth - 20, rowHeight - 30);

    // --- 上段: Filter 比較（スライム）---
    float row1Y = headerHeight + (rowHeight - imgSize) / 2;

    // 行ラベル
    tc::setColor(0.8f, 0.8f, 0.8f);
    tc::drawBitmapString("Filter:", margin, row1Y - 5);

    for (int i = 0; i < 3; i++) {
        float x = margin + i * (colWidth + margin) + (colWidth - imgSize) / 2;

        // 背景
        tc::setColor(0.25f, 0.25f, 0.28f);
        tc::drawRect(x - 3, row1Y - 3, imgSize + 6, imgSize + 6);

        // 画像
        tc::setColor(1.0f, 1.0f, 1.0f);
        if (i == 0) {
            imgNearest_.draw(x, row1Y, imgSize, imgSize);
            tc::setColor(0.4f, 0.8f, 1.0f);
            tc::drawBitmapString("NEAREST", x + imgSize / 2 - 28, row1Y + imgSize + 15);
        } else if (i == 1) {
            imgLinear_.draw(x, row1Y, imgSize, imgSize);
            tc::setColor(1.0f, 0.8f, 0.4f);
            tc::drawBitmapString("LINEAR", x + imgSize / 2 - 24, row1Y + imgSize + 15);
        } else {
            imgCubic_.draw(x, row1Y, imgSize, imgSize);
            tc::setColor(0.8f, 1.0f, 0.4f);
            tc::drawBitmapString("CUBIC", x + imgSize / 2 - 20, row1Y + imgSize + 15);
        }
    }

    // --- 下段: Wrap 比較（レンガ）---
    float row2Y = headerHeight + rowHeight + margin + (rowHeight - imgSize) / 2;

    // 行ラベル
    tc::setColor(0.8f, 0.8f, 0.8f);
    tc::drawBitmapString("Wrap:", margin, row2Y - 5);

    // Wrap モードでは UV を 0-1 範囲外に設定して繰り返しを見せる
    // drawSubsection を使って UV 範囲を拡張
    float uvScale = 4.0f;  // 4x4 タイル分表示

    for (int i = 0; i < 3; i++) {
        float x = margin + i * (colWidth + margin) + (colWidth - imgSize) / 2;

        // 背景
        tc::setColor(0.25f, 0.25f, 0.28f);
        tc::drawRect(x - 3, row2Y - 3, imgSize + 6, imgSize + 6);

        // 画像（UV 範囲を拡張して描画）
        tc::setColor(1.0f, 1.0f, 1.0f);
        if (i == 0) {
            imgBrickRepeat_.getTexture().drawSubsection(x, row2Y, imgSize, imgSize,
                                           0, 0, BRICK_SIZE * uvScale, BRICK_SIZE * uvScale);
            tc::setColor(1.0f, 0.6f, 0.6f);
            tc::drawBitmapString("REPEAT", x + imgSize / 2 - 24, row2Y + imgSize + 15);
        } else if (i == 1) {
            imgBrickClamp_.getTexture().drawSubsection(x, row2Y, imgSize, imgSize,
                                          0, 0, BRICK_SIZE * uvScale, BRICK_SIZE * uvScale);
            tc::setColor(0.6f, 1.0f, 0.6f);
            tc::drawBitmapString("CLAMP", x + imgSize / 2 - 20, row2Y + imgSize + 15);
        } else {
            imgBrickMirrored_.getTexture().drawSubsection(x, row2Y, imgSize, imgSize,
                                             0, 0, BRICK_SIZE * uvScale, BRICK_SIZE * uvScale);
            tc::setColor(0.6f, 0.6f, 1.0f);
            tc::drawBitmapString("MIRRORED", x + imgSize / 2 - 32, row2Y + imgSize + 15);
        }
    }

    // 原寸表示
    tc::setColor(0.5f, 0.5f, 0.5f);
    tc::drawBitmapString("Original:", w - 100, h - 45);
    tc::setColor(1.0f, 1.0f, 1.0f);
    imgOriginal_.draw(w - 100, h - 30, SRC_SIZE, SRC_SIZE);
    imgBrickRepeat_.draw(w - 50, h - 30, BRICK_SIZE * 2, BRICK_SIZE * 2);
}

void tcApp::keyPressed(int key) {
    if (key == SAPP_KEYCODE_UP) {
        scale_ = min(scale_ * 2.0f, 32.0f);
        cout << "Scale: " << scale_ << "x" << endl;
    } else if (key == SAPP_KEYCODE_DOWN) {
        scale_ = max(scale_ / 2.0f, 2.0f);
        cout << "Scale: " << scale_ << "x" << endl;
    } else if (key == '1') {
        scale_ = 4.0f;
    } else if (key == '2') {
        scale_ = 8.0f;
    } else if (key == '3') {
        scale_ = 16.0f;
    } else if (key == '4') {
        scale_ = 32.0f;
    }
}

// ---------------------------------------------------------------------------
// バイキュービック補間の重み関数（Catmull-Rom スプライン）
// ---------------------------------------------------------------------------
float tcApp::cubicWeight(float t) {
    t = fabs(t);
    if (t < 1.0f) {
        return (1.5f * t - 2.5f) * t * t + 1.0f;
    } else if (t < 2.0f) {
        return ((-0.5f * t + 2.5f) * t - 4.0f) * t + 2.0f;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// バイキュービック補間でアップスケール
// ---------------------------------------------------------------------------
void tcApp::upscaleBicubic(const tc::Image& src, tc::Image& dst, int newWidth, int newHeight) {
    int srcW = src.getWidth();
    int srcH = src.getHeight();

    dst.allocate(newWidth, newHeight, 4);

    for (int y = 0; y < newHeight; y++) {
        for (int x = 0; x < newWidth; x++) {
            float srcX = (x + 0.5f) * srcW / newWidth - 0.5f;
            float srcY = (y + 0.5f) * srcH / newHeight - 0.5f;

            int ix = (int)floor(srcX);
            int iy = (int)floor(srcY);
            float fx = srcX - ix;
            float fy = srcY - iy;

            float r = 0, g = 0, b = 0, a = 0;
            float weightSum = 0;

            for (int dy = -1; dy <= 2; dy++) {
                float wy = cubicWeight(fy - dy);
                int sy = clamp(iy + dy, 0, srcH - 1);

                for (int dx = -1; dx <= 2; dx++) {
                    float wx = cubicWeight(fx - dx);
                    int sx = clamp(ix + dx, 0, srcW - 1);

                    float w = wx * wy;
                    tc::Color c = src.getColor(sx, sy);

                    r += c.r * w;
                    g += c.g * w;
                    b += c.b * w;
                    a += c.a * w;
                    weightSum += w;
                }
            }

            if (weightSum > 0) {
                r /= weightSum;
                g /= weightSum;
                b /= weightSum;
                a /= weightSum;
            }

            dst.setColor(x, y, tc::Color(
                clamp(r, 0.0f, 1.0f),
                clamp(g, 0.0f, 1.0f),
                clamp(b, 0.0f, 1.0f),
                clamp(a, 0.0f, 1.0f)
            ));
        }
    }
}

// ---------------------------------------------------------------------------
// ピクセルアート生成（スライム）
// ---------------------------------------------------------------------------
void tcApp::generatePixelArt(tc::Image& img) {
    // 背景を透明に
    for (int y = 0; y < SRC_SIZE; y++) {
        for (int x = 0; x < SRC_SIZE; x++) {
            img.setColor(x, y, tc::Color(0, 0, 0, 0));
        }
    }

    tc::Color body(0.3f, 0.8f, 0.4f, 1.0f);
    tc::Color bodyLight(0.5f, 0.9f, 0.6f, 1.0f);
    tc::Color bodyDark(0.2f, 0.6f, 0.3f, 1.0f);
    tc::Color eye(0.1f, 0.1f, 0.1f, 1.0f);
    tc::Color eyeHighlight(1.0f, 1.0f, 1.0f, 1.0f);
    tc::Color mouth(0.15f, 0.15f, 0.15f, 1.0f);

    for (int x = 5; x <= 10; x++) { img.setColor(x, 4, bodyLight); }
    for (int x = 4; x <= 11; x++) { img.setColor(x, 5, body); }
    for (int x = 3; x <= 12; x++) { img.setColor(x, 6, body); }
    for (int x = 3; x <= 12; x++) { img.setColor(x, 7, body); }
    for (int x = 3; x <= 12; x++) { img.setColor(x, 8, body); }
    for (int x = 3; x <= 12; x++) { img.setColor(x, 9, body); }
    for (int x = 4; x <= 11; x++) { img.setColor(x, 10, body); }
    for (int x = 5; x <= 10; x++) { img.setColor(x, 11, bodyDark); }
    for (int x = 6; x <= 9; x++) { img.setColor(x, 12, bodyDark); }

    img.setColor(5, 5, bodyLight);
    img.setColor(6, 5, bodyLight);
    img.setColor(4, 6, bodyLight);
    img.setColor(5, 6, bodyLight);

    img.setColor(5, 7, eye); img.setColor(6, 7, eye);
    img.setColor(5, 8, eye); img.setColor(6, 8, eye);
    img.setColor(5, 7, eyeHighlight);

    img.setColor(9, 7, eye); img.setColor(10, 7, eye);
    img.setColor(9, 8, eye); img.setColor(10, 8, eye);
    img.setColor(9, 7, eyeHighlight);

    img.setColor(7, 9, mouth);
    img.setColor(8, 9, mouth);

    img.setColor(3, 7, bodyDark); img.setColor(3, 8, bodyDark); img.setColor(3, 9, bodyDark);
    img.setColor(12, 7, bodyDark); img.setColor(12, 8, bodyDark); img.setColor(12, 9, bodyDark);
}

// ---------------------------------------------------------------------------
// レンガパターン生成（8x8）
// ---------------------------------------------------------------------------
void tcApp::generateBrickPattern(tc::Image& img) {
    tc::Color brick(0.8f, 0.4f, 0.3f, 1.0f);      // レンガ色
    tc::Color brickDark(0.6f, 0.3f, 0.2f, 1.0f);  // レンガ暗め
    tc::Color mortar(0.5f, 0.5f, 0.45f, 1.0f);    // 目地

    // 全体を目地色で塗る
    for (int y = 0; y < BRICK_SIZE; y++) {
        for (int x = 0; x < BRICK_SIZE; x++) {
            img.setColor(x, y, mortar);
        }
    }

    // 上段レンガ（0-2行目、左右にずれなし）
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            img.setColor(x, y, (y == 0 || x == 0) ? brickDark : brick);
        }
        for (int x = 4; x < 7; x++) {
            img.setColor(x, y, (y == 0 || x == 4) ? brickDark : brick);
        }
    }

    // 下段レンガ（4-6行目、半分ずらし）
    for (int y = 4; y < 7; y++) {
        // 左端の半分
        for (int x = 0; x < 1; x++) {
            img.setColor(x, y, (y == 4) ? brickDark : brick);
        }
        // 中央
        for (int x = 2; x < 5; x++) {
            img.setColor(x, y, (y == 4 || x == 2) ? brickDark : brick);
        }
        // 右端
        for (int x = 6; x < 8; x++) {
            img.setColor(x, y, (y == 4 || x == 6) ? brickDark : brick);
        }
    }
}
