#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

// =============================================================================
// Custom button (click to count up)
// =============================================================================
class CounterButton : public RectNode {
public:
    using Ptr = shared_ptr<CounterButton>;

    int count = 0;
    string label = "Button";
    Color baseColor = Color(0.3f, 0.3f, 0.4f);
    Color hoverColor = Color(0.4f, 0.4f, 0.6f);
    Color pressColor = Color(0.2f, 0.2f, 0.3f);

    bool isPressed = false;

    CounterButton() {
        enableEvents();  // Enable events
        width = 150;
        height = 50;
    }

    void draw() override {
        // Color based on state
        if (isPressed) {
            setColor(pressColor);
        } else if (isMouseOver()) {
            setColor(hoverColor);
        } else {
            setColor(baseColor);
        }

        // Background
        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // Border
        noFill();
        stroke();
        setColor(0.6f, 0.6f, 0.7f);
        drawRect(0, 0, width, height);

        // Label and count (placed at top-left, follows rotation)
        fill();
        noStroke();
        setColor(1.0f, 1.0f, 1.0f);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %d", label.c_str(), count);
        drawBitmapString(buf, 4, 18, false);  // screenFixed = false follows rotation (baseline-based)
    }

protected:
    bool onMousePress(float localX, float localY, int button) override {
        (void)localX; (void)localY; (void)button;
        isPressed = true;
        count++;
        cout << label << " pressed! count = " << count << endl;
        return true;  // Consume event
    }

    bool onMouseRelease(float localX, float localY, int button) override {
        (void)localX; (void)localY; (void)button;
        isPressed = false;
        return true;
    }
};

// =============================================================================
// Rotating container (inherits RectNode for hit detection)
// =============================================================================
class RotatingPanel : public RectNode {
public:
    using Ptr = shared_ptr<RotatingPanel>;

    float rotationSpeed = 0.3f;
    Color panelColor = Color(0.2f, 0.25f, 0.3f);

    RotatingPanel() {
        enableEvents();
        width = 300;
        height = 200;
    }

    void update() override {
        rotation += (float)getDeltaTime() * rotationSpeed;
    }

    void draw() override {
        // Panel background
        setColor(panelColor);
        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // Border
        noFill();
        stroke();
        setColor(0.5f, 0.5f, 0.6f);
        drawRect(0, 0, width, height);

        // Center mark
        fill();
        setColor(1.0f, 1.0f, 0.0f, 0.5f);
        drawCircle(width / 2, height / 2, 5);
    }
};

// =============================================================================
// Main app
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;

private:
    // Static buttons
    CounterButton::Ptr button1_;
    CounterButton::Ptr button2_;
    CounterButton::Ptr button3_;

    // Buttons inside rotating panel
    RotatingPanel::Ptr panel_;
    CounterButton::Ptr panelButton1_;
    CounterButton::Ptr panelButton2_;

    bool paused_ = false;
};
