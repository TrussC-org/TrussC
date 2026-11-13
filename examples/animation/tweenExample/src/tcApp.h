#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    // Tweens for each easing type
    vector<Tween<float>> tweens;
    vector<EventListener> completeListeners;

    // Animation state
    float startX = 200;
    float endX = 900;
    float duration = 2.0f;
    EaseMode currentMode = EaseMode::InOut;
    int completedCount = 0;

    // Repeat mode: 0=none, 1=loop(3), 2=yoyo infinite
    int repeatMode = 0;

    void setup() override;
    void initTweens();
    void update() override;
    void draw() override;
    void drawMiniGraph(float x, float y, float w, float h, EaseType type, const Color& color);
    string getModeString(EaseMode mode);
    void mousePressed(const MouseEventArgs& e) override;
    void keyPressed(int key) override;
};
