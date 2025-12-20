#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;

private:
    // Demo mode
    int mode_ = 0;
    static constexpr int NUM_MODES = 4;

    // Demo drawing
    void drawLerpComparison();     // Lerp method comparison
    void drawHueWheel();           // Hue wheel HSB vs OKLCH
    void drawLightnessDemo();      // Lightness uniformity demo
    void drawGradientDemo();       // Gradient comparison
};
