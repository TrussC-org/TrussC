#include "tcApp.h"

void tcApp::setup() {
    tcLogNotice("tcApp") << "fboExample: FBO Demo";
    tcLogNotice("tcApp") << "  - Press SPACE to toggle clear() in FBO";
    tcLogNotice("tcApp") << "  - Current: using Fbo::begin(color)";

    // Create FBO (400x300)
    fbo_.allocate(400, 300);
}

void tcApp::update() {
    time_ = getElapsedTime();
}

void tcApp::draw() {
    // Clear the screen
    clear(0.12f, 0.12f, 0.16f);

    // --- Offscreen rendering to FBO ---
    if (useClearInFbo_) {
        // Test calling clear() inside FBO
        fbo_.begin();
        clear(0.2f, 0.1f, 0.3f);  // Clear with purple
    } else {
        // Normal: specify clear color as argument to begin()
        fbo_.begin(0.2f, 0.1f, 0.3f, 1.0f);
    }

    // Draw inside FBO
    // Rotating circles
    int numCircles = 8;
    float centerX = fbo_.getWidth() / 2.0f;
    float centerY = fbo_.getHeight() / 2.0f;
    float radius = 100.0f;

    for (int i = 0; i < numCircles; i++) {
        float angle = (float(i) / numCircles) * TAU + time_;
        float x = centerX + cos(angle) * radius;
        float y = centerY + sin(angle) * radius;

        float hue = float(i) / numCircles * TAU;
        Color c = colorFromHSB(hue, 0.8f, 1.0f);
        setColor(c);
        drawCircle(x, y, 25.0f);
    }

    // White circle in the center
    setColor(1.0f);
    drawCircle(centerX, centerY, 40.0f + sin(time_ * 3.0f) * 10.0f);

    fbo_.end();

    // --- Draw FBO to screen ---
    setColor(1.0f);

    // Top-left: original size
    fbo_.draw(20, 80);

    // Right: scaled down
    fbo_.draw(450, 80, 200, 150);

    // --- Display info ---
    string modeStr = useClearInFbo_ ? "Using clear() in FBO" : "Using Fbo::begin(color)";
    drawBitmapStringHighlight("FBO Example", 10, 20,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
    drawBitmapStringHighlight(modeStr, 10, 40,
        Color(0, 0, 0, 0.7f), useClearInFbo_ ? Color(1, 0.5f, 0.5f) : Color(0.5f, 1, 0.5f));
    drawBitmapStringHighlight("Press SPACE to toggle", 10, 60,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        useClearInFbo_ = !useClearInFbo_;
        if (useClearInFbo_) {
            tcLogNotice("tcApp") << "Mode: Using clear() in FBO (may not work correctly)";
        } else {
            tcLogNotice("tcApp") << "Mode: Using Fbo::begin(color) (correct method)";
        }
    }
}
