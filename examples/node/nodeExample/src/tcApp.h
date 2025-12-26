#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// Rotating container node
// Parent node for child nodes. Can test mouse local coordinates
// =============================================================================
class RotatingContainer : public Node {
public:
    float rotationSpeed = 0.5f;
    float size = 200.0f;

    void update() override {
        setRot(getRot() + (float)getDeltaTime() * rotationSpeed);
    }

    void draw() override {
        // Display container boundary (local coords 0,0 is center)
        noFill();
        stroke();
        setColor(0.5f, 0.5f, 0.5f);
        drawRect(-size/2, -size/2, size, size);
        fill();
        noStroke();

        // Center point
        setColor(1.0f, 1.0f, 0.0f);
        drawCircle(0, 0, 5);

        // Display local coordinate axes
        setColor(1.0f, 0.3f, 0.3f);  // X-axis = Red
        drawLine(0, 0, 50, 0);
        setColor(0.3f, 1.0f, 0.3f);  // Y-axis = Green
        drawLine(0, 0, 0, 50);

        // Title (displayed above rectangle)
        setColor(1.0f, 1.0f, 1.0f, 0.8f);
        drawBitmapString("Local Coord System", -size/2, -size/2 - 12, false);
    }
};

// =============================================================================
// Mouse follower node (uses local coordinates)
// =============================================================================
class MouseFollower : public Node {
public:
    float radius = 15.0f;
    float r = 0.3f, g = 0.7f, b = 1.0f;

    void draw() override {
        // getMouseX/Y() returns local coords considering parent transform
        // Draws at correct position even if parent is rotated
        float mx = getMouseX();
        float my = getMouseY();

        // Parent container size (RotatingContainer's size/2 is boundary)
        float bound = 125.0f;  // About half of container size
        bool insideBox = (mx >= -bound && mx <= bound && my >= -bound && my <= bound);

        setColor(r, g, b, 0.8f);
        drawCircle(mx, my, radius);

        // Center point
        setColor(1.0f, 1.0f, 1.0f);
        drawCircle(mx, my, 3);

        // Show local coords only when inside rectangle
        if (insideBox) {
            setColor(1.0f, 1.0f, 1.0f, 0.9f);
            drawBitmapString(format("local: {:.0f}, {:.0f}", mx, my), mx, my);
        }
    }
};

// =============================================================================
// Fixed position child node (placed in local coords)
// =============================================================================
class FixedChild : public Node {
public:
    float size = 30.0f;
    float hue = 0.0f;

    void draw() override {
        // Set color based on hue
        float r = (sin(hue) * 0.5f + 0.5f);
        float g = (sin(hue + TAU / 3) * 0.5f + 0.5f);
        float b = (sin(hue + TAU * 2 / 3) * 0.5f + 0.5f);

        setColor(r, g, b);
        drawRect(-size/2, -size/2, size, size);
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
    void mousePressed(Vec2 pos, int button) override;
    void mouseDragged(Vec2 pos, int button) override;

private:
    // Nodes
    std::shared_ptr<RotatingContainer> container1_;
    std::shared_ptr<RotatingContainer> container2_;
    std::shared_ptr<MouseFollower> follower1_;
    std::shared_ptr<MouseFollower> follower2_;
};
