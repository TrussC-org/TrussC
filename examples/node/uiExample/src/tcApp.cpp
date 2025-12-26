// =============================================================================
// uiExample - UI Components Sample Implementation
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // -------------------------------------------------------------------------
    // Button 1: Click counter
    // -------------------------------------------------------------------------
    button1_ = make_shared<UIButton>();
    button1_->label = "Click Me!";
    button1_->setRect(50, 50, 140, 45);
    button1_->onClick = [this]() {
        clickCount_++;
        button1_->label = "Clicked: " + to_string(clickCount_);
        tcLogNotice("tcApp") << "Button1 clicked! Count: " << clickCount_;
    };
    addChild(button1_);

    // -------------------------------------------------------------------------
    // Button 2: Reset background color
    // -------------------------------------------------------------------------
    button2_ = make_shared<UIButton>();
    button2_->label = "Reset BG";
    button2_->setRect(50, 110, 140, 45);
    button2_->normalColor = Color(0.3f, 0.25f, 0.25f);
    button2_->hoverColor = Color(0.45f, 0.35f, 0.35f);
    button2_->pressColor = Color(0.2f, 0.15f, 0.15f);
    button2_->onClick = [this]() {
        bgColor_ = Color(0.1f, 0.1f, 0.12f);
        slider1_->setValue(0.1f);
        slider2_->setValue(0.1f);
        tcLogNotice("tcApp") << "Background reset!";
    };
    addChild(button2_);

    // -------------------------------------------------------------------------
    // Slider 1: Background color R component
    // -------------------------------------------------------------------------
    slider1_ = make_shared<UISlider>();
    slider1_->label = "BG Red";
    slider1_->setRect(50, 200, 250, 30);
    slider1_->minValue = 0.0f;
    slider1_->maxValue = 0.5f;
    slider1_->setValue(bgColor_.r);
    slider1_->onValueChanged = [this](float v) {
        bgColor_.r = v;
    };
    addChild(slider1_);

    // -------------------------------------------------------------------------
    // Slider 2: Background color G component
    // -------------------------------------------------------------------------
    slider2_ = make_shared<UISlider>();
    slider2_->label = "BG Green";
    slider2_->setRect(50, 260, 250, 30);
    slider2_->minValue = 0.0f;
    slider2_->maxValue = 0.5f;
    slider2_->setValue(bgColor_.g);
    slider2_->onValueChanged = [this](float v) {
        bgColor_.g = v;
    };
    addChild(slider2_);

    // -------------------------------------------------------------------------
    // Scroll box: Item list
    // -------------------------------------------------------------------------
    scrollBox_ = make_shared<UIScrollBox>();
    scrollBox_->setRect(350, 50, 250, 200);
    scrollBox_->contentHeight = 300;  // 10 items x 30px
    addChild(scrollBox_);

    tcLogNotice("tcApp") << "=== uiExample ===";
    tcLogNotice("tcApp") << "UI components demo with event handling";
    tcLogNotice("tcApp") << "- Click buttons to trigger events";
    tcLogNotice("tcApp") << "- Drag sliders or use scroll wheel to change values";
    tcLogNotice("tcApp") << "- Scroll inside the box to see items";
    tcLogNotice("tcApp") << "- Press R to reset";
}

void tcApp::update() {
    // Child nodes are updated automatically (called by updateTree)
}

void tcApp::draw() {
    // Background color
    clear(bgColor_);

    // Description text
    setColor(0.7f, 0.7f, 0.75f);
    drawBitmapString("Buttons: Click to trigger events", 50, 170);
    drawBitmapString("Sliders: Drag or scroll wheel to change value", 50, 310);
    drawBitmapString("ScrollBox: Mouse wheel to scroll content", 350, 270);

    // Display current background color
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString(format("Background: R={:.2f} G={:.2f} B={:.2f}",
             bgColor_.r, bgColor_.g, bgColor_.b), 50, 350);

    // Frame rate
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString(format("FPS: {:.1f}", getFrameRate()), getWindowWidth() - 100, 30);

    // Child nodes (UI components) are drawn automatically (called by drawTree)
}

void tcApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        // Reset with R key
        bgColor_ = Color(0.1f, 0.1f, 0.12f);
        slider1_->setValue(0.1f);
        slider2_->setValue(0.1f);
        clickCount_ = 0;
        button1_->label = "Click Me!";
        tcLogNotice("tcApp") << "Reset!";
    }
}

// Mouse events are automatically dispatched by TrussC.h
