#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

// =============================================================================
// ClipBox - Rectangle node with clipping enabled
// =============================================================================
class ClipBox : public RectNode {
public:
    using Ptr = shared_ptr<ClipBox>;

    Color bgColor = Color(0.2f, 0.2f, 0.25f);
    Color borderColor = Color(0.5f, 0.5f, 0.6f);
    string label;

    ClipBox() {
        setClipping(true);  // Enable clipping
    }

    void draw() override {
        // Background
        setColor(bgColor);
        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // Label
        if (!label.empty()) {
            setColor(1.0f, 1.0f, 1.0f, 0.7f);
            drawBitmapString(label, 5, 15, false);
        }
    }

    // Separate method to draw border after children
    void drawBorder() {
        noFill();
        stroke();
        setColor(borderColor);
        drawRect(0, 0, width, height);
    }
};

// =============================================================================
// BouncingCircle - Bouncing circle (for clipping test)
// =============================================================================
class BouncingCircle : public RectNode {
public:
    using Ptr = shared_ptr<BouncingCircle>;

    float radius = 30;
    Color color = Color(0.8f, 0.4f, 0.2f);
    float vx = 2.0f;
    float vy = 1.5f;
    float boundsWidth = 300;
    float boundsHeight = 200;

    void update() override {
        // Movement
        x += vx;
        y += vy;

        // Bounce
        if (x - radius < -50 || x + radius > boundsWidth + 50) vx = -vx;
        if (y - radius < -50 || y + radius > boundsHeight + 50) vy = -vy;
    }

    void draw() override {
        setColor(color);
        fill();
        noStroke();
        drawCircle(0, 0, radius);

        // Center point
        setColor(1.0f, 1.0f, 1.0f);
        drawCircle(0, 0, 3);
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
    // Outer clip box
    ClipBox::Ptr outerBox_;

    // Inner clip box (rotating)
    ClipBox::Ptr innerBox_;

    // Bouncing circles
    vector<BouncingCircle::Ptr> circles_;
};
