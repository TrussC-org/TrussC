#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// CrossHatch - Fixed size content with crosshatch pattern
// =============================================================================
class CrossHatch : public RectNode {
public:
    using Ptr = shared_ptr<CrossHatch>;

    float gridSize = 50.0f;
    Color bgColor = Color(0.15f, 0.15f, 0.18f);
    Color lineColor = Color(0.3f, 0.3f, 0.35f);

    CrossHatch(float w, float h) {
        setSize(w, h);
    }

    void draw() override {
        // Background
        setColor(bgColor);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        // Grid lines
        setColor(lineColor);
        noFill();

        // Vertical lines
        for (float x = 0; x <= getWidth(); x += gridSize) {
            drawLine(x, 0, x, getHeight());
        }

        // Horizontal lines
        for (float y = 0; y <= getHeight(); y += gridSize) {
            drawLine(0, y, getWidth(), y);
        }

        // Border
        setColor(0.5f, 0.5f, 0.55f);
        drawRect(0, 0, getWidth(), getHeight());

        // Size label
        setColor(0.6f, 0.6f, 0.65f);
        drawBitmapString(format("{:.0f}x{:.0f}", getWidth(), getHeight()), 10, 20);
    }
};

// =============================================================================
// ListItem - Simple item for scroll list
// =============================================================================
class ListItem : public RectNode {
public:
    using Ptr = shared_ptr<ListItem>;

    string label;
    Color bgColor = Color(0.25f, 0.25f, 0.3f);
    int index = 0;

    ListItem(int idx, float width, float height) : index(idx) {
        setSize(width, height);
        enableEvents();
        label = format("Item {}", idx + 1);

        // Vary color slightly by index
        float hue = fmod(idx * 0.07f, 1.0f);
        bgColor = Color::fromHSB(hue, 0.3f, 0.35f);
    }

    void draw() override {
        // Background
        Color color = isMouseOver() ? bgColor * 1.3f : bgColor;
        setColor(color);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        // Border
        noFill();
        setColor(0.5f, 0.5f, 0.55f);
        drawRect(0, 0, getWidth(), getHeight());

        // Label
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString(label, 15, getHeight() / 2 + 4);

        // Index on right
        setColor(0.6f, 0.6f, 0.65f);
        drawBitmapString(format("#{}", index + 1), getWidth() - 50, getHeight() / 2 + 4);
    }

protected:
    bool onMousePress(Vec2 local, int button) override {
        logNotice("ListItem") << "Clicked: " << label;
        return RectNode::onMousePress(local, button);
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
    // Vertical scroll demo
    ScrollContainer::Ptr vScrollContainer_;
    RectNode::Ptr vContent_;
    LayoutMod* vLayout_ = nullptr;
    ScrollBar::Ptr vScrollBar_;

    // Horizontal scroll demo
    ScrollContainer::Ptr hScrollContainer_;
    RectNode::Ptr hContent_;
    LayoutMod* hLayout_ = nullptr;
    ScrollBar::Ptr hScrollBar_;

    // Both directions scroll demo
    ScrollContainer::Ptr bothScrollContainer_;
    CrossHatch::Ptr crossHatch_;
    ScrollBar::Ptr bothVScrollBar_;
    ScrollBar::Ptr bothHScrollBar_;

    int itemCount_ = 0;

    void addItem();
    void removeItem();
    void addHItem();
    void removeHItem();
};
