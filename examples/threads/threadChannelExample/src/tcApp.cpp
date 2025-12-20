#include "tcApp.h"
#include <cmath>

// ---------------------------------------------------------------------------
// setup - Initialization
// ---------------------------------------------------------------------------
void tcApp::setup() {
    sourcePixels_.resize(AnalysisThread::TOTAL_PIXELS);
}

// ---------------------------------------------------------------------------
// update - Update
// ---------------------------------------------------------------------------
void tcApp::update() {
    frameNum_++;

    // Generate pattern (sin wave + noise)
    float t = frameNum_ * 0.05f;
    for (int i = 0; i < AnalysisThread::TOTAL_PIXELS; i++) {
        float ux = (i % AnalysisThread::WIDTH) / (float)AnalysisThread::WIDTH;
        float uy = (i / AnalysisThread::WIDTH) / (float)AnalysisThread::HEIGHT;

        // Wave pattern
        float value = std::sin(ux * 10.0f + t) * std::sin(uy * 8.0f + t * 0.7f);
        value = (value + 1.0f) * 0.5f;  // Normalize to 0-1
        sourcePixels_[i] = value;
    }

    // Send analysis request
    analyzer_.analyze(sourcePixels_);

    // Receive analysis result
    analyzer_.update();
}

// ---------------------------------------------------------------------------
// draw - Drawing
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.1f);

    // Draw original pattern (left side)
    setColor(1.0f);
    drawBitmapString("Source (Main Thread)", 20, 20);

    for (int py = 0; py < AnalysisThread::HEIGHT; py++) {
        for (int px = 0; px < AnalysisThread::WIDTH; px++) {
            int i = py * AnalysisThread::WIDTH + px;
            float value = sourcePixels_[i];
            setColor(value, value, value);
            drawRect(20 + px * 4, 40 + py * 4, 4, 4);
        }
    }

    // Draw analysis result (right side)
    setColor(1.0f);
    drawBitmapString("Analyzed (Worker Thread)", 300, 20);
    analyzer_.draw(300, 40);

    // Display information
    setColor(0.78f, 0.78f, 0.78f);
    drawBitmapString("Frame: " + toString(frameNum_), 20, 260);
    drawBitmapString("Analyzed: " + toString(analyzer_.getAnalyzedCount()), 20, 275);

    setColor(0.4f, 0.78f, 0.4f);
    drawBitmapString("ThreadChannel Demo:", 20, 310);
    drawBitmapString("  Main -> Worker: source pixels", 20, 325);
    drawBitmapString("  Worker -> Main: thresholded result", 20, 340);
}
