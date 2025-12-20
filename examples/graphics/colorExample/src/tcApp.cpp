#include "tcApp.h"
#include <iostream>

using namespace std;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "04_color: Color Space Demo" << endl;
    cout << "  - Space: Switch mode" << endl;
    cout << "  - ESC: Exit" << endl;
    cout << endl;
    cout << "Modes:" << endl;
    cout << "  0: Lerp comparison (RGB/Linear/HSB/OKLab/OKLCH)" << endl;
    cout << "  1: Hue wheel (HSB vs OKLCH)" << endl;
    cout << "  2: Lightness uniformity (OKLab feature)" << endl;
    cout << "  3: Gradient comparison" << endl;
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
// Lerp method comparison
// ---------------------------------------------------------------------------
void tcApp::drawLerpComparison() {
    // Color definition methods:
    //   Color c = Color(1.0f, 0.5f, 0.0f);           // float (0-1)
    //   Color c = Color::fromBytes(255, 127, 0);    // int (0-255)
    //   Color c = Color::fromHex(0xFF7F00);         // HEX code
    //   Color c = colors::orange;                   // Predefined color

    // Choose 2 colors (red -> cyan shows the difference clearly)
    Color c1 = colors::red;
    Color c2 = colors::cyan;

    float startX = 100;
    float endX = 1180;
    float barHeight = 60;
    float y = 80;
    float gap = 100;
    int steps = 256;

    const char* labels[] = {
        "lerpRGB (sRGB space - not recommended)",
        "lerpLinear (Linear space - physically correct)",
        "lerpHSB (HSB space)",
        "lerpOKLab (OKLab space - default)",
        "lerpOKLCH (OKLCH space - preserves hue)"
    };

    for (int mode = 0; mode < 5; mode++) {
        // Draw gradient bar
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

        // Label
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(labels[mode], startX, y + barHeight + 8);

        y += gap;
    }

    // Display start/end colors
    setColor(c1);
    drawRect(30, 80, 50, 50);
    setColor(c2);
    drawRect(30, 140, 50, 50);
}

// ---------------------------------------------------------------------------
// Hue wheel HSB vs OKLCH
// ---------------------------------------------------------------------------
void tcApp::drawHueWheel() {
    float centerX1 = 320;
    float centerX2 = 960;
    float centerY = 360;
    float radius = 250;
    int segments = 360;

    // HSB hue wheel
    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i / segments * TAU;
        float angle2 = (float)(i + 1) / segments * TAU;

        // HSB: Use hue directly
        Color c = ColorHSB(angle1, 1.0f, 1.0f).toRGB();
        setColor(c);

        // Draw fan shape
        float x1 = centerX1 + cos(angle1) * radius;
        float y1 = centerY + sin(angle1) * radius;
        float x2 = centerX1 + cos(angle2) * radius;
        float y2 = centerY + sin(angle2) * radius;
        drawTriangle(centerX1, centerY, x1, y1, x2, y2);
    }

    // OKLCH hue wheel
    for (int i = 0; i < segments; i++) {
        float angle1 = (float)i / segments * TAU;
        float angle2 = (float)(i + 1) / segments * TAU;

        // OKLCH: Equalize saturation with L=0.7, C=0.15
        Color c = ColorOKLCH(0.7f, 0.15f, angle1).toRGB().clamped();
        setColor(c);

        float x1 = centerX2 + cos(angle1) * radius;
        float y1 = centerY + sin(angle1) * radius;
        float x2 = centerX2 + cos(angle2) * radius;
        float y2 = centerY + sin(angle2) * radius;
        drawTriangle(centerX2, centerY, x1, y1, x2, y2);
    }

    // Label (semi-transparent black background)
    drawBitmapStringHighlight("HSB", centerX1 - 12, centerY - 6,
        Color(0, 0, 0, 0.5f), Color(1, 1, 1));
    drawBitmapStringHighlight("OKLCH", centerX2 - 20, centerY - 6,
        Color(0, 0, 0, 0.5f), Color(1, 1, 1));
}

// ---------------------------------------------------------------------------
// Lightness uniformity demo
// ---------------------------------------------------------------------------
void tcApp::drawLightnessDemo() {
    float startX = 100;
    float barWidth = 1080;
    float barHeight = 80;
    int segments = 360;
    float segmentWidth = barWidth / segments;

    // HSB: Same brightness (B=1) but perceptual lightness varies
    float y1 = 150;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
        setColor(c);
        drawRect(startX + i * segmentWidth, y1, segmentWidth + 1, barHeight);
    }

    // Convert HSB to grayscale to check lightness
    float y2 = 250;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
        // Luminance calculation (sRGB)
        float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        setColor(luma, luma, luma);
        drawRect(startX + i * segmentWidth, y2, segmentWidth + 1, barHeight);
    }

    // OKLCH: Perceptually uniform lightness with same L
    float y3 = 400;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorOKLCH(0.7f, 0.15f, hue).toRGB().clamped();
        setColor(c);
        drawRect(startX + i * segmentWidth, y3, segmentWidth + 1, barHeight);
    }

    // Convert OKLCH to grayscale
    float y4 = 500;
    for (int i = 0; i < segments; i++) {
        float hue = (float)i / segments * TAU;
        Color c = ColorOKLCH(0.7f, 0.15f, hue).toRGB().clamped();
        float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        setColor(luma, luma, luma);
        drawRect(startX + i * segmentWidth, y4, segmentWidth + 1, barHeight);
    }

    // Labels
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("HSB (B=1.0, S=1.0)", startX, y1 - 20);
    drawBitmapString("HSB -> Grayscale", startX, y2 - 20);
    drawBitmapString("OKLCH (L=0.7, C=0.15)", startX, y3 - 20);
    drawBitmapString("OKLCH -> Grayscale", startX, y4 - 20);
}

// ---------------------------------------------------------------------------
// Gradient comparison
// ---------------------------------------------------------------------------
void tcApp::drawGradientDemo() {
    // Compare with multiple color pairs
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

        // Left column: OKLab (default)
        for (int i = 0; i < steps; i++) {
            float t = (float)i / (steps - 1);
            Color c = pair.c1.lerpOKLab(pair.c2, t);
            setColor(c);
            drawRect(startX + i * stepWidth, y, stepWidth + 1, barHeight);
        }

        // Right column: RGB comparison
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

    // Legend
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("OKLab / HSB", startX, 25);
    drawBitmapString("RGB / OKLCH", startX + colGap, 25);
}

// ---------------------------------------------------------------------------
// Input
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
