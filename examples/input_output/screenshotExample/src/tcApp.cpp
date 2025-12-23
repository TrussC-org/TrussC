#include "tcApp.h"

namespace fs = std::filesystem;

void tcApp::setup() {
    tcLogNotice("tcApp") << "screenshotExample: saveScreenshot() Demo";
    tcLogNotice("tcApp") << "  - Press SPACE to capture screenshot";
    tcLogNotice("tcApp") << "  - Uses OS window capture (no FBO needed)";

    // Save path (data folder)
    savePath = getDataPath("");
    tcLogNotice("tcApp") << "Screenshots will be saved to: " << savePath.string();
}

void tcApp::update() {
    time = getElapsedTime();
}

void tcApp::draw() {
    // Clear background (dark blue-purple)
    clear(51, 51, 76);

    // Demo drawing: rotating circles
    int numCircles = 12;
    float centerX = getWindowWidth() / 2.0f;
    float centerY = getWindowHeight() / 2.0f;
    float radius = 150.0f;

    for (int i = 0; i < numCircles; i++) {
        float angle = (float(i) / numCircles) * TAU + time;
        float x = centerX + cos(angle) * radius;
        float y = centerY + sin(angle) * radius;

        // Change hue
        float hue = float(i) / numCircles * TAU;
        Color c = colorFromHSB(hue, 0.8f, 1.0f);
        setColor(c);

        float circleRadius = 30.0f + sin(time * 2.0f + i) * 10.0f;
        drawCircle(x, y, circleRadius);
    }

    // Large circle in center
    setColor(1.0f);
    drawCircle(centerX, centerY, 50.0f + sin(time * 3.0f) * 20.0f);

    // Grid lines
    setColor(1.0f, 1.0f, 1.0f, 0.2f);
    for (int x = 0; x < getWindowWidth(); x += 50) {
        drawLine(x, 0, x, getWindowHeight());
    }
    for (int y = 0; y < getWindowHeight(); y += 50) {
        drawLine(0, y, getWindowWidth(), y);
    }

    // Display information
    drawBitmapStringHighlight("saveScreenshot() Demo", 10, 20,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));

    string sizeStr = "Window: " + to_string(getWindowWidth()) + "x" + to_string(getWindowHeight());
    drawBitmapStringHighlight(sizeStr, 10, 40,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));

    drawBitmapStringHighlight("Press SPACE to capture", 10, 60,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));

    string countStr = "Saved: " + to_string(screenshotCount);
    drawBitmapStringHighlight(countStr, 10, 80,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        string filename = "screenshot_" + to_string(screenshotCount) + ".png";
        fs::path filepath = savePath / filename;

        // Save screenshot using OS window capture feature
        if (saveScreenshot(filepath)) {
            tcLogNotice("tcApp") << "Saved: " << filepath.string();
            screenshotCount++;
        } else {
            tcLogWarning("tcApp") << "Failed to save: " << filepath.string();
        }
    }
}
