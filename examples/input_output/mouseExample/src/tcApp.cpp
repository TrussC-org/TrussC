#include "tcApp.h"
#include <sstream>
#include <algorithm>

void tcApp::setup() {
    setWindowTitle("mouseExample");
}

void tcApp::draw() {
    clear(30);

    int w = getWindowWidth();
    int h = getWindowHeight();

    // Draw circle that changes with scroll
    float scrollSize = 50 + scrollY * 2;  // Size changes with scroll
    if (scrollSize < 10) scrollSize = 10;
    if (scrollSize > 300) scrollSize = 300;

    float hue = fmod(scrollX * 0.1f, TAU);
    if (hue < 0) hue += TAU;
    Color scrollColor = ColorHSB(hue, 0.8f, 0.9f).toRGB();

    setColor(scrollColor);
    drawCircle(w - 100, h / 2, scrollSize);
    setColor(1.0f);
    drawBitmapString("Scroll\nto change", w - 130, h / 2 - 20);

    // Draw drag trail
    if (dragTrail.size() > 1) {
        for (size_t i = 1; i < dragTrail.size(); i++) {
            auto& p0 = dragTrail[i - 1];
            auto& p1 = dragTrail[i];

            // Change color by button
            if (p1.button == MOUSE_BUTTON_LEFT) {
                setColor(colors::red);
            } else if (p1.button == MOUSE_BUTTON_MIDDLE) {
                setColor(colors::green);
            } else {
                setColor(colors::blue);
            }
            drawLine(p0.x, p0.y, p1.x, p1.y);
        }
    }

    // Draw click positions (with fade out)
    for (auto& cp : clickPoints) {
        if (cp.button == MOUSE_BUTTON_LEFT) {
            setColor(1.0f, 0.4f, 0.4f, cp.alpha);
        } else if (cp.button == MOUSE_BUTTON_MIDDLE) {
            setColor(0.4f, 1.0f, 0.4f, cp.alpha);
        } else {
            setColor(0.4f, 0.4f, 1.0f, cp.alpha);
        }
        drawCircle(cp.x, cp.y, 20 * cp.alpha + 5);
        cp.alpha -= 0.01f;
    }

    // Remove clicks that have faded out
    clickPoints.erase(
        std::remove_if(clickPoints.begin(), clickPoints.end(),
            [](const ClickPoint& cp) { return cp.alpha <= 0; }),
        clickPoints.end()
    );

    // Display cursor at current mouse position
    float mx = getGlobalMouseX();
    float my = getGlobalMouseY();

    setColor(1.0f);
    drawLine(mx - 10, my, mx + 10, my);
    drawLine(mx, my - 10, mx, my + 10);

    // Display mouse information
    setColor(1.0f);
    drawBitmapString("=== Mouse Input Demo ===", 20, 20);

    std::stringstream ss;
    ss << "Position: (" << (int)mx << ", " << (int)my << ")";
    drawBitmapString(ss.str(), 20, 50);

    ss.str("");
    ss << "Previous: (" << (int)getGlobalPMouseX() << ", " << (int)getGlobalPMouseY() << ")";
    drawBitmapString(ss.str(), 20, 65);

    ss.str("");
    ss << "Button: " << (isMousePressed() ? std::to_string(getMouseButton()) : "none");
    ss << "  Dragging: " << (isDragging ? "yes" : "no");
    drawBitmapString(ss.str(), 20, 80);

    // Scroll value
    ss.str("");
    ss << "Scroll: X=" << (int)scrollX << " Y=" << (int)scrollY;
    drawBitmapString(ss.str(), 20, 95);

    // Instructions
    setColor(0.6f);
    drawBitmapString("Left drag: red trail", 20, h - 70);
    drawBitmapString("Middle drag: green trail", 20, h - 55);
    drawBitmapString("Right drag: blue trail", 20, h - 40);
    drawBitmapString("Scroll to accumulate scroll value", 20, h - 25);

    // Button legend
    setColor(colors::red);
    drawRect(w - 150, 20, 20, 20);
    setColor(0.0f);
    drawBitmapString("Left", w - 120, 25);

    setColor(colors::green);
    drawRect(w - 150, 45, 20, 20);
    setColor(0.0f);
    drawBitmapString("Middle", w - 120, 50);

    setColor(colors::blue);
    drawRect(w - 150, 70, 20, 20);
    setColor(0.0f);
    drawBitmapString("Right", w - 120, 75);
}

void tcApp::mousePressed(Vec2 pos, int button) {
    isDragging = true;
    currentButton = button;
    dragTrail.clear();
    dragTrail.push_back({pos.x, pos.y, button});

    clickPoints.push_back({pos.x, pos.y, button, 1.0f});
}

void tcApp::mouseReleased(Vec2 pos, int button) {
    isDragging = false;
    currentButton = -1;
}

void tcApp::mouseMoved(Vec2 pos) {
    // Nothing to do on move
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    if (isDragging) {
        dragTrail.push_back({pos.x, pos.y, button});

        // Remove old points if trail gets too long
        if (dragTrail.size() > 500) {
            dragTrail.erase(dragTrail.begin());
        }
    }
}

void tcApp::mouseScrolled(Vec2 delta) {
    scrollX += delta.x;
    scrollY += delta.y;

    // Range limit (corresponds to size 10-300: scrollY = -20 to 125)
    if (scrollY < -20) scrollY = -20;
    if (scrollY > 125) scrollY = 125;
}
