#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// SoundButton - RectNode-based sound button with automatic hit testing
// =============================================================================
class SoundButton : public RectNode {
public:
    using Ptr = shared_ptr<SoundButton>;

    Sound sound;
    string label;
    bool isLoop = false;
    float playEndTime = 0;

    // Colors
    Color normalColor = Color(0.2f, 0.3f, 0.4f);
    Color playingColor = Color(0.4f, 0.7f, 0.4f);
    Color borderColor = Color(0.5f, 0.6f, 0.7f);

    SoundButton() {
        enableEvents();
    }

    bool isCurrentlyPlaying() {
        return sound.isPlaying() || getElapsedTime() < playEndTime;
    }

    void draw() override {
        // Background
        if (isCurrentlyPlaying()) {
            setColor(playingColor);
        } else {
            setColor(normalColor);
        }
        drawRectFill();

        // Border
        setColor(borderColor);
        drawRectStroke();

        // Label
        setColor(1.0f);
        drawBitmapString(label, 10, height / 2 - 5);
    }

protected:
    bool onMousePress(Vec2 local, int button) override {
        if (isLoop) {
            // Toggle for loop sounds
            if (sound.isPlaying()) {
                sound.stop();
            } else {
                sound.play();
            }
        } else {
            // One-shot
            sound.stop();
            sound.play();
            playEndTime = getElapsedTime() + sound.getDuration();
        }
        return RectNode::onMousePress(local, button);
    }
};

// =============================================================================
// tcApp
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void draw() override;

private:
    void createSounds();
    void addSectionLabel(const string& text, float y);

    vector<SoundButton::Ptr> allButtons;

    float buttonWidth = 110;
    float buttonHeight = 40;
    float margin = 8;
};
