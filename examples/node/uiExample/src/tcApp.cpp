// =============================================================================
// uiExample - UI Components with ScrollContainer and LayoutMod
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    // Buttons
    button1_ = make_shared<UIButton>();
    button1_->label = "Click Me!";
    button1_->setRect(50, 50, 140, 45);
    button1_->onClick = [this]() {
        clickCount_++;
        button1_->label = "Clicked: " + to_string(clickCount_);
    };
    addChild(button1_);

    button2_ = make_shared<UIButton>();
    button2_->label = "Reset BG";
    button2_->setRect(50, 110, 140, 45);
    button2_->normalColor = Color(0.3f, 0.25f, 0.25f);
    button2_->hoverColor = Color(0.45f, 0.35f, 0.35f);
    button2_->onClick = [this]() {
        bgColor_ = Color(0.1f, 0.1f, 0.12f);
        slider1_->setValue(0.1f);
        slider2_->setValue(0.1f);
    };
    addChild(button2_);

    // Sliders
    slider1_ = make_shared<UISlider>();
    slider1_->label = "BG Red";
    slider1_->setRect(50, 200, 250, 30);
    slider1_->maxValue = 0.5f;
    slider1_->setValue(bgColor_.r);
    slider1_->onValueChanged = [this](float v) { bgColor_.r = v; };
    addChild(slider1_);

    slider2_ = make_shared<UISlider>();
    slider2_->label = "BG Green";
    slider2_->setRect(50, 260, 250, 30);
    slider2_->maxValue = 0.5f;
    slider2_->setValue(bgColor_.g);
    slider2_->onValueChanged = [this](float v) { bgColor_.g = v; };
    addChild(slider2_);

    // ScrollContainer with LayoutMod
    scrollContainer_ = make_shared<ScrollContainer>();
    scrollContainer_->setRect(350, 50, 250, 200);
    addChild(scrollContainer_);

    scrollContent_ = make_shared<RectNode>();
    scrollContent_->setSize(250, 0);
    layout_ = scrollContent_->addMod<LayoutMod>(LayoutDirection::Vertical, 4.0f);
    layout_->setPadding(5);
    layout_->setMainAxis(AxisMode::Content);
    scrollContainer_->setContent(scrollContent_);

    // Add items
    for (int i = 0; i < 10; i++) {
        scrollContent_->addChild(make_shared<ListItem>(i, 230, 30));
    }
    layout_->updateLayout();

    // ScrollBar
    scrollBar_ = make_shared<ScrollBar>(scrollContainer_.get(), ScrollBar::Vertical);
    scrollContainer_->addChild(scrollBar_);

    logNotice("tcApp") << "=== uiExample (refactored) ===";
}

void tcApp::update() {
    scrollContainer_->updateScrollBounds();
    scrollBar_->updateFromContainer();
}

void tcApp::draw() {
    clear(bgColor_);

    setColor(0.7f, 0.7f, 0.75f);
    drawBitmapString("Buttons: Click to trigger events", 50, 170);
    drawBitmapString("Sliders: Drag or scroll to change", 50, 310);
    drawBitmapString("ScrollList: Wheel or drag scrollbar", 350, 270);

    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString(format("Background: R={:.2f} G={:.2f}", bgColor_.r, bgColor_.g), 50, 350);

    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString(format("FPS: {:.1f}", getFrameRate()), getWidth() - 100, 30);
}

void tcApp::keyPressed(int key) {
    if (key == 'R') {
        bgColor_ = Color(0.1f, 0.1f, 0.12f);
        slider1_->setValue(0.1f);
        slider2_->setValue(0.1f);
        clickCount_ = 0;
        button1_->label = "Click Me!";
    }
}
