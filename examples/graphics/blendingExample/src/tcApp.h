#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>
#include <string>

using namespace std;

// =============================================================================
// blendingExample - Blend mode comparison demo
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Get blend mode name
    string getBlendModeName(BlendMode mode);

    // Demo parameters
    float animTime_ = 0.0f;
};
