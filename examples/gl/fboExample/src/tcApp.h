#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>

using namespace std;

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
    bool test1Done_ = false;
    bool test2Done_ = false;
};
