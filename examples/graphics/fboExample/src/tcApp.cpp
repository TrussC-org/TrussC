#include "tcApp.h"

void tcApp::setup() {
    logNotice("tcApp") << "fboExample: FBO Demo";

    // Create FBO (400x300)
    fbo_.allocate(400, 300);

    // Draw content to FBO once in setup
    fbo_.begin(0.2f, 0.15f, 0.25f, 1.0f);

    // Draw grid
    setColor(0.4f, 0.35f, 0.5f);
    float gridSize = 20.0f;
    for (float x = 0; x <= fbo_.getWidth(); x += gridSize) {
        drawLine(x, 0, x, fbo_.getHeight());
    }
    for (float y = 0; y <= fbo_.getHeight(); y += gridSize) {
        drawLine(0, y, fbo_.getWidth(), y);
    }

    // Draw large circle in center
    float centerX = fbo_.getWidth() / 2.0f;
    float centerY = fbo_.getHeight() / 2.0f;
    setColor(0.9f, 0.6f, 0.2f);
    drawCircle(centerX, centerY, 80.0f);

    fbo_.end();
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.12f, 0.12f, 0.16f);

    setColor(1.0f);

    // Left: original size
    fbo_.draw(30, 80);

    // Right top: half size
    fbo_.draw(480, 80, 200, 150);

    // Right bottom: subsection (top-left 200x150 of FBO)
    fbo_.getTexture().drawSubsection(480, 280, 200, 150, 0, 0, 200, 150);

    // Labels
    setColor(1.0f);
    drawBitmapString("FBO Example - Offscreen Rendering", 30, 30);
    drawBitmapString("Original (400x300)", 30, 60);
    drawBitmapString("Half (200x150)", 480, 60);
    drawBitmapString("Subsection (top-left)", 480, 260);
}

void tcApp::keyPressed(int key) {
}
