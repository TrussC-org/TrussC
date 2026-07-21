// =============================================================================
// roundedRectExample
// Compares four rectangle drawing methods:
//   - drawRect: sharp corners
//   - drawRectRounded: circular arc corners
//   - drawRectSquircle: curvature-continuous corners (iOS-style)
//   - drawSuperellipse: Lamé curve inscribed in the rect (Apple-icon-like)
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Rounded Rect Example");
    // Default tolerance (0.1 px) keeps corners visually crisp at this size;
    // bump down to 0.05 if you want extra-smooth high-DPI output.
    // setCurveTolerance(0.05f);
}

void tcApp::draw() {
    clear(0.15f, 0.15f, 0.2f);

    float w = 180, h = 180, r = 36;
    float y = 120;

    // 1. Normal
    setColor(1.0f);
    drawBitmapString("drawRect", 60, y - 20);
    setColor(0.9f, 0.3f, 0.3f);
    drawRect(60, y, w, h);

    // 2. Rounded (circular arc)
    setColor(1.0f);
    drawBitmapString("drawRectRounded", 280, y - 20);
    setColor(0.3f, 0.9f, 0.3f);
    drawRectRounded(280, y, w, h, r);

    // 3. Squircle (iOS-style)
    setColor(1.0f);
    drawBitmapString("drawRectSquircle", 500, y - 20);
    setColor(0.3f, 0.5f, 0.9f);
    drawRectSquircle(500, y, w, h, r);

    // 4. Superellipse (Apple-icon-like, default n=5)
    setColor(1.0f);
    drawBitmapString("drawSuperellipse", 720, y - 20);
    setColor(0.9f, 0.7f, 0.2f);
    drawSuperellipse(720, y, w, h);

    // Exponent morph: n from concave star to near-rect
    float ns[] = {0.7f, 1.0f, 2.0f, 2.5f, 4.0f, 5.0f, 10.0f};
    float sw = 100, sy = 420;
    setColor(1.0f);
    drawBitmapString("drawSuperellipse(x, y, w, h, n) with varying n", 60, sy - 20);
    for (int i = 0; i < 7; i++) {
        float sx = 60 + i * (sw + 25);
        setColor(0.9f, 0.7f, 0.2f);
        drawSuperellipse(sx, sy, sw, sw, ns[i]);
        setColor(1.0f);
        drawBitmapString("n=" + toString(ns[i], 1), sx + 30, sy + sw + 24);
    }
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) sapp_request_quit();
}
