// =============================================================================
// clippingExample - Scissor Clipping demo implementation
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    tcLogNotice("tcApp") << "=== clippingExample ===";
    tcLogNotice("tcApp") << "Nested Scissor Clipping Demo";

    // Outer clip box
    outerBox_ = make_shared<ClipBox>();
    outerBox_->setRect(80, 80, 450, 350);
    outerBox_->bgColor = Color(0.15f, 0.15f, 0.2f);
    outerBox_->borderColor = Color(0.3f, 0.5f, 0.8f);
    outerBox_->label = "Outer ClipBox";
    addChild(outerBox_);

    // Inner clip box
    innerBox_ = make_shared<ClipBox>();
    innerBox_->setRect(80, 60, 280, 200);
    innerBox_->bgColor = Color(0.2f, 0.15f, 0.15f);
    innerBox_->borderColor = Color(0.8f, 0.5f, 0.3f);
    innerBox_->label = "Inner ClipBox";
    outerBox_->addChild(innerBox_);

    // Add bouncing circles to inner box
    for (int i = 0; i < 5; i++) {
        auto circle = make_shared<BouncingCircle>();
        circle->setPos(50 + i * 40, 50 + i * 25);
        circle->radius = 18 + i * 4;
        circle->vx = 1.5f + i * 0.3f;
        circle->vy = 1.0f + i * 0.4f;
        circle->boundsWidth = innerBox_->getWidth();
        circle->boundsHeight = innerBox_->getHeight();
        circle->color = colorFromHSB(i * 0.15f, 0.7f, 0.9f);
        innerBox_->addChild(circle);
        circles_.push_back(circle);
    }

    // Add circle directly to outer box (outer clipping test)
    auto outerCircle = make_shared<BouncingCircle>();
    outerCircle->setPos(350, 260);
    outerCircle->radius = 35;
    outerCircle->vx = -1.2f;
    outerCircle->vy = 0.8f;
    outerCircle->boundsWidth = outerBox_->getWidth();
    outerCircle->boundsHeight = outerBox_->getHeight();
    outerCircle->color = Color(0.3f, 0.8f, 0.4f);
    outerBox_->addChild(outerCircle);
    circles_.push_back(outerCircle);
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.08f, 0.08f, 0.1f);

    // Title
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Nested Scissor Clipping Demo", 20, 25);

    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Circles are clipped by BOTH outer and inner boxes", 20, 45);

    // Legend on the right
    setColor(0.3f, 0.5f, 0.8f);
    drawBitmapString("Blue = Outer clip", 600, 120);
    setColor(0.8f, 0.5f, 0.3f);
    drawBitmapString("Orange = Inner clip", 600, 140);
    setColor(0.3f, 0.8f, 0.4f);
    drawBitmapString("Green = Outer only", 600, 160);
}

void tcApp::keyPressed(int key) {
}
