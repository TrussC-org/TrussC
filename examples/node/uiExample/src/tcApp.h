#pragma once

#include <TrussC.h>
#include <functional>
using namespace std;
using namespace tc;

// =============================================================================
// UIButton - Simple button with click callback
// =============================================================================
class UIButton : public RectNode {
public:
    using Ptr = shared_ptr<UIButton>;

    string label = "Button";
    Color normalColor = Color(0.25f, 0.25f, 0.3f);
    Color hoverColor = Color(0.35f, 0.35f, 0.45f);
    Color pressColor = Color(0.15f, 0.15f, 0.2f);
    function<void()> onClick;

    UIButton() {
        enableEvents();
        setSize(120, 40);
    }

    void draw() override {
        setColor(isPressed_ ? pressColor : isMouseOver() ? hoverColor : normalColor);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        noFill();
        setColor(0.5f, 0.5f, 0.6f);
        drawRect(0, 0, getWidth(), getHeight());

        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(label, getWidth() / 2 - label.length() * 4, getHeight() / 2 + 4, false);
    }

protected:
    bool isPressed_ = false;

    bool onMousePress(const MouseEventArgs& e) override {
        isPressed_ = true;
        return RectNode::onMousePress(e);
    }

    bool onMouseRelease(const MouseEventArgs& e) override {
        if (isPressed_ && isMouseOver() && onClick) onClick();
        isPressed_ = false;
        return RectNode::onMouseRelease(e);
    }
};

// =============================================================================
// UISlider - Slider with drag and scroll support
// =============================================================================
class UISlider : public RectNode {
public:
    using Ptr = shared_ptr<UISlider>;

    float value = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    string label = "Slider";
    function<void(float)> onValueChanged;

    UISlider() {
        enableEvents();
        setSize(200, 30);
    }

    float getValue() const { return minValue + value * (maxValue - minValue); }
    void setValue(float v) { value = clamp((v - minValue) / (maxValue - minValue), 0.0f, 1.0f); }

    void draw() override {
        setColor(0.2f, 0.2f, 0.25f);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        setColor(0.4f, 0.4f, 0.45f);
        drawRect(0, getHeight() / 2 - 2, getWidth(), 4);

        float knobX = value * getWidth();
        setColor(isDragging_ ? Color(0.6f, 0.7f, 0.9f) : Color(0.5f, 0.6f, 0.8f));
        drawRect(knobX - 6, 2, 12, getHeight() - 4);

        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(format("{}: {:.2f}", label, getValue()), 4, -17, false);
    }

protected:
    bool isDragging_ = false;

    bool onMousePress(const MouseEventArgs& e) override {
        isDragging_ = true;
        updateValue(e.pos.x);
        return true;
    }

    bool onMouseRelease(const MouseEventArgs& e) override {
        (void)e;
        isDragging_ = false;
        return true;
    }

    bool onMouseDrag(const MouseDragEventArgs& e) override {
        if (isDragging_) updateValue(e.pos.x);
        return true;
    }

    bool onMouseScroll(const ScrollEventArgs& e) override {
        float old = value;
        value = clamp(value + e.scroll.y * 0.05f, 0.0f, 1.0f);
        if (value != old && onValueChanged) onValueChanged(getValue());
        return true;
    }

private:
    void updateValue(float lx) {
        float old = value;
        value = clamp(lx / getWidth(), 0.0f, 1.0f);
        if (value != old && onValueChanged) onValueChanged(getValue());
    }
};

// =============================================================================
// ListItem - Item for scroll list
// =============================================================================
class ListItem : public RectNode {
public:
    using Ptr = shared_ptr<ListItem>;
    string label;
    Color color;

    ListItem(int index, float w, float h) {
        setSize(w, h);
        label = format("Item {}", index + 1);
        color = Color::fromHSB(index * 0.08f, 0.4f, 0.5f);
    }

    void draw() override {
        setColor(color);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(label, 10, getHeight() / 2 + 4);
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
    UIButton::Ptr button1_;
    UIButton::Ptr button2_;
    UISlider::Ptr slider1_;
    UISlider::Ptr slider2_;

    ScrollContainer::Ptr scrollContainer_;
    RectNode::Ptr scrollContent_;
    ScrollBar::Ptr scrollBar_;
    LayoutMod* layout_ = nullptr;

    int clickCount_ = 0;
    Color bgColor_ = Color(0.1f, 0.1f, 0.12f);
};
