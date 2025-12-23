#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <functional>

using namespace std;

// =============================================================================
// UIButton - Button that fires event on click
// =============================================================================
class UIButton : public RectNode {
public:
    using Ptr = shared_ptr<UIButton>;

    string label = "Button";
    Color normalColor = Color(0.25f, 0.25f, 0.3f);
    Color hoverColor = Color(0.35f, 0.35f, 0.45f);
    Color pressColor = Color(0.15f, 0.15f, 0.2f);

    // Click event (register callback from outside)
    function<void()> onClick;

    UIButton() {
        enableEvents();
        width = 120;
        height = 40;
    }

    void update() override {
        // No manual hover detection needed - use isMouseOver() in draw()
    }

    void draw() override {
        // Color based on state
        if (isPressed_) {
            setColor(pressColor);
        } else if (isMouseOver()) {
            setColor(hoverColor);
        } else {
            setColor(normalColor);
        }

        fill();
        noStroke();
        drawRect(0, 0, width, height);

        // Border
        noFill();
        stroke();
        setColor(0.5f, 0.5f, 0.6f);
        drawRect(0, 0, width, height);

        // Label
        fill();
        setColor(1.0f, 1.0f, 1.0f);
        float textX = width / 2 - label.length() * 4;
        drawBitmapString(label, textX, height / 2 + 5, false);
    }

protected:
    bool isPressed_ = false;

    bool onMousePress(Vec2 local, int button) override {
        isPressed_ = true;
        return RectNode::onMousePress(local, button);
    }

    bool onMouseRelease(Vec2 local, int button) override {
        if (isPressed_ && isMouseOver() && onClick) {
            onClick();  // Fire click event
        }
        isPressed_ = false;
        return RectNode::onMouseRelease(local, button);
    }
};

// =============================================================================
// UISlider - Slider to change value by drag & scroll
// =============================================================================
class UISlider : public RectNode {
public:
    using Ptr = shared_ptr<UISlider>;

    float value = 0.5f;  // 0.0 ~ 1.0
    float minValue = 0.0f;
    float maxValue = 1.0f;
    string label = "Slider";

    // Value change event
    function<void(float)> onValueChanged;

    UISlider() {
        enableEvents();
        width = 200;
        height = 30;
    }

    float getValue() const {
        return minValue + value * (maxValue - minValue);
    }

    void setValue(float v) {
        value = (v - minValue) / (maxValue - minValue);
        value = std::max<float>(0.0f, std::min<float>(1.0f, value));
    }

    // Receive scroll event from outside
    void handleScroll(float dx, float dy) {
        (void)dx;  // Unused
        float delta = dy * 0.05f;
        float oldValue = value;
        value = std::max<float>(0.0f, std::min<float>(1.0f, value + delta));
        if (value != oldValue && onValueChanged) {
            onValueChanged(getValue());
        }
    }

    void draw() override {
        // Background
        setColor(0.2f, 0.2f, 0.25f);
        fill();
        drawRect(0, 0, width, height);

        // Track
        float trackY = height / 2;
        float trackH = 4;
        setColor(0.4f, 0.4f, 0.45f);
        drawRect(0, trackY - trackH / 2, width, trackH);

        // Knob
        float knobX = value * width;
        float knobW = 12;
        float knobH = height - 4;
        setColor(isDragging_ ? Color(0.6f, 0.7f, 0.9f) : Color(0.5f, 0.6f, 0.8f));
        drawRect(knobX - knobW / 2, 2, knobW, knobH);

        // Label and value
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(format("{}: {:.2f}", label, getValue()), 4, -4, false);
    }

protected:
    bool isDragging_ = false;

    bool onMousePress(Vec2 local, int button) override {
        isDragging_ = true;
        updateValue(local.x);
        return RectNode::onMousePress(local, button);
    }

    bool onMouseRelease(Vec2 local, int button) override {
        isDragging_ = false;
        return RectNode::onMouseRelease(local, button);
    }

    bool onMouseDrag(Vec2 local, int button) override {
        if (isDragging_) {
            updateValue(local.x);
        }
        return RectNode::onMouseDrag(local, button);
    }

    bool onMouseMove(Vec2 local) override {
        // Update value while dragging
        if (isDragging_) {
            updateValue(local.x);
        }
        return RectNode::onMouseMove(local);
    }

    bool onMouseScroll(Vec2 local, Vec2 scroll) override {
        // Change value by scroll
        float delta = scroll.y * 0.05f;
        float oldValue = value;
        value = std::max<float>(0.0f, std::min<float>(1.0f, value + delta));
        if (value != oldValue && onValueChanged) {
            onValueChanged(getValue());
        }
        return RectNode::onMouseScroll(local, scroll);
    }

private:
    void updateValue(float lx) {
        float oldValue = value;
        value = std::max<float>(0.0f, std::min<float>(1.0f, lx / width));
        if (value != oldValue && onValueChanged) {
            onValueChanged(getValue());
        }
    }
};

// =============================================================================
// UIScrollBox - Box that scrolls content by scroll
// =============================================================================
class UIScrollBox : public RectNode {
public:
    using Ptr = shared_ptr<UIScrollBox>;

    float scrollY = 0;
    float contentHeight = 300;  // Internal content height

    UIScrollBox() {
        enableEvents();
        width = 200;
        height = 150;
    }

    // Receive scroll event from outside
    void handleScroll(float dx, float dy) {
        (void)dx;  // Unused
        float maxScroll = std::max<float>(0.0f, contentHeight - height);
        scrollY = std::max<float>(0.0f, std::min<float>(maxScroll, scrollY - dy * 20));
    }

    void draw() override {
        // Background
        setColor(0.15f, 0.15f, 0.18f);
        fill();
        drawRect(0, 0, width, height);

        // Set clipping (in global coordinates, considering DPI scale)
        float gx, gy;
        localToGlobal(0, 0, gx, gy);
        float dpi = sapp_dpi_scale();
        pushScissor(gx * dpi, gy * dpi, width * dpi, height * dpi);

        // Scrollable content
        pushMatrix();
        translate(0, -scrollY);

        // Content (multiple items) - automatically hidden outside range by clipping
        for (int i = 0; i < 10; i++) {
            float itemY = i * 30;
            setColor(0.3f + i * 0.05f, 0.3f, 0.35f);
            fill();
            drawRect(5, itemY + 2, width - 10, 26);

            setColor(1.0f, 1.0f, 1.0f);
            drawBitmapString(format("Item {}", i + 1), 10, itemY + 18, false);
        }

        popMatrix();

        // Restore clipping
        popScissor();

        // Border
        noFill();
        stroke();
        setColor(0.4f, 0.4f, 0.5f);
        drawRect(0, 0, width, height);

        // Scrollbar
        float maxScroll = std::max<float>(0.0f, contentHeight - height);
        if (maxScroll > 0) {
            float barHeight = height * (height / contentHeight);
            float barY = (scrollY / maxScroll) * (height - barHeight);
            fill();
            setColor(0.5f, 0.5f, 0.6f);
            drawRect(width - 8, barY, 6, barHeight);
        }
    }

protected:
    bool onMouseScroll(Vec2 local, Vec2 scroll) override {
        float maxScroll = std::max<float>(0.0f, contentHeight - height);
        scrollY = std::max<float>(0.0f, std::min<float>(maxScroll, scrollY - scroll.y * 20));
        return RectNode::onMouseScroll(local, scroll);
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
    void mouseReleased(Vec2 pos, int button) override;
    void mouseMoved(Vec2 pos) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseScrolled(Vec2 delta) override;

private:
    UIButton::Ptr button1_;
    UIButton::Ptr button2_;
    UISlider::Ptr slider1_;
    UISlider::Ptr slider2_;
    UIScrollBox::Ptr scrollBox_;

    int clickCount_ = 0;
    Color bgColor_ = Color(0.1f, 0.1f, 0.12f);
};
