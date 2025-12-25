#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

    void keyPressed(int key) override;

private:
    // Demo mode
    int mode_ = 0;
    static constexpr int NUM_MODES = 4;

    // Gradient colors (adjustable via ImGui)
    float color1_[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // Red
    float color2_[4] = {0.0f, 1.0f, 1.0f, 1.0f};  // Cyan

    // Demo drawing
    void drawLerpComparison();     // Lerp method comparison
    void drawHueWheel();           // Hue wheel HSB vs OKLCH
    void drawLightnessDemo();      // Lightness uniformity demo
    void drawGradientDemo();       // Gradient comparison

    // ImGui
    void drawGui();
};
