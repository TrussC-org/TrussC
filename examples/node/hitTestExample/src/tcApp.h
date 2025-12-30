#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

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
        setSize(150, 50);
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
        drawRect(0, 0, getWidth(), getHeight());

        // Border
        noFill();
        stroke();
        setColor(0.6f, 0.6f, 0.7f);
        drawRect(0, 0, getWidth(), getHeight());

        // Label and count (placed at top-left, follows rotation)
        fill();
        noStroke();
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(format("{}: {}", label, count), 4, 18, false);  // screenFixed = false follows rotation (baseline-based)
    }

protected:
    bool onMousePress(Vec2 local, int button) override {
        (void)local; (void)button;
        isPressed = true;
        count++;
        logNotice("Button") << label << " pressed! count = " << count;
        return true;  // Consume event
    }

    bool onMouseRelease(Vec2 local, int button) override {
        (void)local; (void)button;
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
        setSize(300, 200);
    }

    void update() override {
        setRot(getRot() + (float)getDeltaTime() * rotationSpeed);
    }

    void draw() override {
        // Panel background
        setColor(panelColor);
        fill();
        noStroke();
        drawRect(0, 0, getWidth(), getHeight());

        // Border
        noFill();
        stroke();
        setColor(0.5f, 0.5f, 0.6f);
        drawRect(0, 0, getWidth(), getHeight());
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
