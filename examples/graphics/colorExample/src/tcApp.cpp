#include "tcApp.h"
#include <iostream>

using namespace std;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "04_color: Color Space Demo" << endl;
    cout << "  - Space: モード切り替え" << endl;
    cout << "  - ESC: 終了" << endl;
    cout << endl;
    cout << "モード:" << endl;
    cout << "  0: Lerp比較 (RGB/Linear/HSB/OKLab/OKLCH)" << endl;
    cout << "  1: 色相環 (HSB vs OKLCH)" << endl;
    cout << "  2: 明度均一性 (OKLabの特徴)" << endl;
    cout << "  3: グラデーション比較" << endl;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void tcApp::update() {
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.15f, 0.15f, 0.18f);

    switch (mode_) {
        case 0: drawLerpComparison(); break;
        case 1: drawHueWheel(); break;
        case 2: drawLightnessDemo(); break;
        case 3: drawGradientDemo(); break;
    }
}

// ---------------------------------------------------------------------------
// Lerp 方式の比較
// ---------------------------------------------------------------------------
void tcApp::drawLerpComparison() {
    // 色の定義方法:
    //   Color c = Color(1.0f, 0.5f, 0.0f);           // float (0-1)
    //   Color c = Color::fromBytes(255, 127, 0);    // int (0-255)
    //   Color c = Color::fromHex(0xFF7F00);         // HEX コード
    //   Color c = colors::orange;                   // 定義済みカラー

    // 2色を選ぶ（赤 → シアン は違いが分かりやすい）
    Color c1 = colors::red;
    Color c2 = colors::cyan;

    float startX = 100;
    float endX = 1180;
    float barHeight = 60;
    float y = 80;
    float gap = 100;
    int steps = 256;

    const char* labels[] = {
        "lerpRGB (sRGB空間 - 非推奨)",
        "lerpLinear (リニア空間 - 物理的に正しい)",
        "lerpHSB (HSB空間)",
        "lerpOKLab (OKLab空間 - デフォルト)",
        "lerpOKLCH (OKLCH空間 - 色相維持)"
    };

    for (int mode = 0; mode < 5; mode++) {
        // グラデーションバーを描画
        float stepWidth = (endX - startX) / steps;

        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c;

            switch (mode) {
                case 0: c = c1.lerpRGB(c2, t); break;
                case 1: c = c1.lerpLinear(c2, t); break;
                case 2: c = c1.lerpHSB(c2, t); break;
                case 3: c = c1.lerpOKLab(c2, t); break;
                case 4: c = c1.lerpOKLCH(c2, t); break;
            }

            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // ラベル
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(labels[mode], startX, y + barHeight + 8);

        y += gap;
    }

    // 開始・終了色を表示
    setColor(c1);
    drawRect(30, 80, 50, 50);
    setColor(c2);
    drawRect(30, 140, 50, 50);
}

// ---------------------------------------------------------------------------
// 色相環 HSB vs OKLCH
// ---------------------------------------------------------------------------
void tcApp::drawHueWheel() {
    float centerX1 = 320;
    float centerX2 = 960;
    float centerY = 360;
    float radius = 250;
    int segments = 360;

    // HSB 色相環
    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i / segments * TAU;
        float angle2 = (float)(i + 1) / segments * TAU;

        // HSB: 色相を直接使用
        Color c = ColorHSB(angle1, 1.0f, 1.0f).toRGB();
        setColor(c);

        // 扇形を描画
        float x1 = centerX1 + cos(angle1) * radius;
        float y1 = centerY + sin(angle1) * radius;
        float x2 = centerX1 + cos(angle2) * radius;
        float y2 = centerY + sin(angle2) * radius;
        drawTriangle(centerX1, centerY, x1, y1, x2, y2);
    }

    // OKLCH 色相環
    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i / segments * TAU;
        float angle2 = (float)(i + 1) / segments * TAU;

        // OKLCH: L=0.7, C=0.15 で彩度を揃える
        Color c = ColorOKLCH(0.7f, 0.15f, angle1).toRGB().clamped();
        setColor(c);

        float x1 = centerX2 + cos(angle1) * radius;
        float y1 = centerY + sin(angle1) * radius;
        float x2 = centerX2 + cos(angle2) * radius;
        float y2 = centerY + sin(angle2) * radius;
        drawTriangle(centerX2, centerY, x1, y1, x2, y2);
    }

    // ラベル（黒半透明背景）
    drawBitmapStringHighlight("HSB", centerX1 - 12, centerY - 6,
        Color(0, 0, 0, 0.5f), Color(1, 1, 1));
    drawBitmapStringHighlight("OKLCH", centerX2 - 20, centerY - 6,
        Color(0, 0, 0, 0.5f), Color(1, 1, 1));
}

// ---------------------------------------------------------------------------
// 明度均一性デモ
// ---------------------------------------------------------------------------
void tcApp::drawLightnessDemo() {
    float startX = 100;
    float barWidth = 1080;
    float barHeight = 80;
    int segments = 360;
    float segmentWidth = barWidth / segments;

    // HSB: 同じ明度(B=1)でも知覚的な明るさが異なる
    float y1 = 150;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
        setColor(c);
        drawRect(startX + i * segmentWidth, y1, segmentWidth + 1, barHeight);
    }

    // HSB をグレースケールに変換して明度を確認
    float y2 = 250;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
        // 輝度計算 (sRGB)
        float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        setColor(luma, luma, luma);
        drawRect(startX + i * segmentWidth, y2, segmentWidth + 1, barHeight);
    }

    // OKLCH: 同じ L で知覚的に均一な明るさ
    float y3 = 400;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorOKLCH(0.7f, 0.15f, hue).toRGB().clamped();
        setColor(c);
        drawRect(startX + i * segmentWidth, y3, segmentWidth + 1, barHeight);
    }

    // OKLCH をグレースケールに変換
    float y4 = 500;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorOKLCH(0.7f, 0.15f, hue).toRGB().clamped();
        float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        setColor(luma, luma, luma);
        drawRect(startX + i * segmentWidth, y4, segmentWidth + 1, barHeight);
    }

    // ラベル
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("HSB (B=1.0, S=1.0)", startX, y1 - 20);
    drawBitmapString("HSB -> Grayscale", startX, y2 - 20);
    drawBitmapString("OKLCH (L=0.7, C=0.15)", startX, y3 - 20);
    drawBitmapString("OKLCH -> Grayscale", startX, y4 - 20);
}

// ---------------------------------------------------------------------------
// グラデーション比較
// ---------------------------------------------------------------------------
void tcApp::drawGradientDemo() {
    // 複数のカラーペアで比較
    struct ColorPair {
        Color c1, c2;
        const char* name;
    };

    ColorPair pairs[] = {
        { colors::red, colors::blue, "Red -> Blue" },
        { colors::yellow, colors::magenta, "Yellow -> Magenta" },
        { Color(0.2f, 0.8f, 0.2f), Color(0.8f, 0.2f, 0.8f), "Green -> Purple" },
        { colors::white, colors::black, "White -> Black" },
    };

    float startX = 150;
    float barWidth = 500;
    float barHeight = 30;
    int steps = 64;
    float stepWidth = barWidth / steps;

    float y = 60;
    float colGap = 550;

    for (int p = 0; p < 4; p++) {
        auto& pair = pairs[p];

        // 左列: OKLab (デフォルト)
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpOKLab(pair.c2, t);
            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // 右列: RGB比較
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpRGB(pair.c2, t);
            setColor(c);
            drawRect(startX + colGap + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        y += 50;

        // HSB
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpHSB(pair.c2, t);
            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // OKLCH
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpOKLCH(pair.c2, t);
            setColor(c);
            drawRect(startX + colGap + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        y += 100;
    }

    // 凡例
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("OKLab / HSB", startX, 25);
    drawBitmapString("RGB / OKLCH", startX + colGap, 25);
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == KEY_SPACE) {
        mode_ = (mode_ + 1) % NUM_MODES;
        cout << "Mode: " << mode_ << endl;
    }
}
