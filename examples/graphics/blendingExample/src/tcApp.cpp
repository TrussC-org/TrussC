// =============================================================================
// blendingExample - Blend mode comparison demo
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
}

void tcApp::update() {
    animTime_ += getDeltaTime();
}

void tcApp::draw() {
    clear(0.15f, 0.15f, 0.15f);

    float w = getWindowWidth();
    float h = getWindowHeight();

    // Title
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Blend Mode Comparison", 20, 25);

    // Blend modes to demonstrate
    BlendMode modes[] = {
        BlendMode::Alpha,
        BlendMode::Add,
        BlendMode::Multiply,
        BlendMode::Screen,
        BlendMode::Subtract,
        BlendMode::Disabled
    };

    int numModes = 6;
    float colWidth = w / numModes;
    float startY = 65;
    float contentH = h - startY - 70;  // Space for content (minus bottom text area)

    // Draw background pattern (base for each column)
    for (int i = 0; i < numModes; i++) {
        float x = i * colWidth;

        // Background gradient (dark -> bright) - cover full content height
        setBlendMode(BlendMode::Disabled);
        int numRows = 10;
        float rowH = contentH / numRows;
        for (int j = 0; j < numRows; j++) {
            float gray = 0.1f + j * 0.08f;
            setColor(gray, gray, gray);
            drawRect(x, startY + j * rowH, colWidth - 8, rowH);
        }

        // Colorful background elements - distributed throughout
        setBlendMode(BlendMode::Alpha);
        setColor(0.8f, 0.2f, 0.2f, 0.7f);
        drawRect(x + 8, startY + 20, 55, 55);
        setColor(0.2f, 0.8f, 0.2f, 0.7f);
        drawRect(x + 40, startY + contentH * 0.25f, 55, 55);
        setColor(0.2f, 0.2f, 0.8f, 0.7f);
        drawRect(x + 70, startY + contentH * 0.5f, 55, 55);
        setColor(0.6f, 0.2f, 0.6f, 0.7f);
        drawRect(x + 20, startY + contentH * 0.7f, 55, 55);
    }

    // Draw circles with each blend mode - distributed from top to bottom
    for (int i = 0; i < numModes; i++) {
        float x = i * colWidth + colWidth / 2;

        // Mode name label
        setBlendMode(BlendMode::Alpha);
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(getBlendModeName(modes[i]), i * colWidth + 8, startY - 12);

        setBlendMode(modes[i]);

        // Animated white circle - travels through the gradient
        float anim = sin(animTime_ * 1.0f + i * 0.5f) * 0.5f + 0.5f;
        setColor(1.0f, 1.0f, 1.0f, 0.7f);
        drawCircle(x, startY + 40 + anim * (contentH - 80), 44);

        // Circles distributed throughout the gradient (tighter spacing)
        setColor(1.0f, 0.3f, 0.3f, 0.7f);
        drawCircle(x - 15, startY + contentH * 0.22f, 39);

        setColor(0.3f, 1.0f, 0.3f, 0.7f);
        drawCircle(x + 15, startY + contentH * 0.36f, 39);

        setColor(0.3f, 0.3f, 1.0f, 0.7f);
        drawCircle(x - 15, startY + contentH * 0.50f, 39);

        setColor(1.0f, 1.0f, 0.3f, 0.6f);
        drawCircle(x + 15, startY + contentH * 0.64f, 42);

        setColor(0.3f, 1.0f, 1.0f, 0.6f);
        drawCircle(x, startY + contentH * 0.78f, 44);
    }

    resetBlendMode();

    // Description text (compact, 2 columns)
    setColor(0.6f, 0.6f, 0.6f);
    drawBitmapString("Alpha: Standard transparency", 20, h - 55);
    drawBitmapString("Add: Brightens (glow)", 20, h - 40);
    drawBitmapString("Multiply: Darkens (shadow)", 20, h - 25);
    drawBitmapString("Screen: Brightens (inv Multiply)", w/2, h - 55);
    drawBitmapString("Subtract: Darkens by sub", w/2, h - 40);
    drawBitmapString("Disabled: Overwrites", w/2, h - 25);
}

void tcApp::keyPressed(int key) {
}

string tcApp::getBlendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Alpha: return "Alpha";
        case BlendMode::Add: return "Add";
        case BlendMode::Multiply: return "Multiply";
        case BlendMode::Screen: return "Screen";
        case BlendMode::Subtract: return "Subtract";
        case BlendMode::Disabled: return "Disabled";
        default: return "Unknown";
    }
}
