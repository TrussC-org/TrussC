// =============================================================================
// clippingExample - Scissor Clipping demo implementation
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    cout << "=== clippingExample ===" << endl;
    cout << "Nested Scissor Clipping Demo" << endl;
    cout << "- Outer box clips inner content" << endl;
    cout << "- Inner box also clips its content" << endl;
    cout << "- Circles only visible in intersection of both boxes" << endl;
    cout << "[R] reset positions" << endl;

    // Outer clip box
    outerBox_ = make_shared<ClipBox>();
    outerBox_->x = 100;
    outerBox_->y = 100;
    outerBox_->width = 500;
    outerBox_->height = 400;
    outerBox_->bgColor = Color(0.15f, 0.15f, 0.2f);
    outerBox_->borderColor = Color(0.3f, 0.5f, 0.8f);
    outerBox_->label = "Outer ClipBox";
    addChild(outerBox_);

    // Inner clip box (rotating)
    innerBox_ = make_shared<ClipBox>();
    innerBox_->x = 100;
    innerBox_->y = 80;
    innerBox_->width = 300;
    innerBox_->height = 220;
    innerBox_->bgColor = Color(0.2f, 0.15f, 0.15f);
    innerBox_->borderColor = Color(0.8f, 0.5f, 0.3f);
    innerBox_->label = "Inner ClipBox (rotating)";
    outerBox_->addChild(innerBox_);

    // Add bouncing circles to inner box
    for (int i = 0; i < 5; i++) {
        auto circle = make_shared<BouncingCircle>();
        circle->x = 50 + i * 50;
        circle->y = 50 + i * 30;
        circle->radius = 20 + i * 5;
        circle->vx = 1.5f + i * 0.3f;
        circle->vy = 1.0f + i * 0.4f;
        circle->boundsWidth = innerBox_->width;
        circle->boundsHeight = innerBox_->height;
        circle->color = colorFromHSB(i * 0.15f, 0.7f, 0.9f);
        innerBox_->addChild(circle);
        circles_.push_back(circle);
    }

    // Add circle directly to outer box (outer clipping test)
    auto outerCircle = make_shared<BouncingCircle>();
    outerCircle->x = 400;
    outerCircle->y = 300;
    outerCircle->radius = 40;
    outerCircle->vx = -1.2f;
    outerCircle->vy = 0.8f;
    outerCircle->boundsWidth = outerBox_->width;
    outerCircle->boundsHeight = outerBox_->height;
    outerCircle->color = Color(0.3f, 0.8f, 0.4f);
    outerBox_->addChild(outerCircle);
    circles_.push_back(outerCircle);
}

void tcApp::update() {
    // Circles are automatically updated via updateTree
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);

    // Title
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Nested Scissor Clipping Demo", 20, 30);

    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Circles are clipped by BOTH outer and inner boxes", 20, 50);
    drawBitmapString("Inner box circles only visible in intersection area", 20, 65);

    // Description on the right
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString("[R] reset positions", 700, 100);

    // Clipping status display
    setColor(0.3f, 0.5f, 0.8f);
    drawBitmapString("Blue = Outer clip boundary", 700, 180);
    setColor(0.8f, 0.5f, 0.3f);
    drawBitmapString("Orange = Inner clip boundary", 700, 200);

    // Child nodes are drawn automatically
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        // Reset
        for (size_t i = 0; i < circles_.size(); i++) {
            circles_[i]->x = 50 + i * 50;
            circles_[i]->y = 50 + i * 30;
        }
        cout << "Reset" << endl;
    }
}
