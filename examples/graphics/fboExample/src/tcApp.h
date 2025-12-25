#pragma once

#include <TrussC.h>
#include <iostream>
using namespace std;
using namespace tc;

// =============================================================================
// fboExample - FBO (Frame Buffer Object) sample
// =============================================================================
// - Render offscreen to FBO and display on screen
// - Test clear() behavior inside FBO
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Fbo fbo_;
    float time_ = 0;
    bool useClearInFbo_ = false;  // Whether to call clear() inside FBO
};
