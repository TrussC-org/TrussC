#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// AnimBox - Animated box that uses TweenMod
// =============================================================================
class AnimBox : public RectNode {
public:
    using Ptr = shared_ptr<AnimBox>;

    Color boxColor = Color(0.4f, 0.5f, 0.7f);
    string label;
    TweenMod* tween = nullptr;

    AnimBox(float size = 60) {
        setSize(size, size);
        tween = addMod<TweenMod>();
    }

    void draw() override {
        // Background with alpha
        Color c = boxColor;
        if (tween && tween->isPlaying()) {
            c = c * 1.2f;  // Brighter when animating
        }
        setColor(c);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        // Border
        noFill();
        setColor(0.8f, 0.8f, 0.9f);
        drawRect(0, 0, getWidth(), getHeight());

        // Label
        if (!label.empty()) {
            setColor(1.0f, 1.0f, 1.0f);
            drawBitmapString(label, getWidth() / 2 - label.length() * 4, getHeight() / 2 + 4);
        }
    }
};

// =============================================================================
// EaseDemo - Shows different easing types
// =============================================================================
struct EaseDemo {
    AnimBox::Ptr box;
    string name;
    EaseType type;
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
    // Main demo boxes
    AnimBox::Ptr moveBox_;
    AnimBox::Ptr scaleBox_;
    AnimBox::Ptr rotateBox_;
    AnimBox::Ptr comboBox_;

    // Easing comparison
    vector<EaseDemo> easeDemos_;

    // State
    bool animating_ = false;
    float baseX_ = 100;
    float targetX_ = 400;

    void startAnimations();
    void resetPositions();
};
