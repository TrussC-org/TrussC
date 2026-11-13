#pragma once

#include <TrussC.h>
#include "shaders/effects.glsl.h"
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    static constexpr int NUM_EFFECTS = 4;

    FullscreenShader shaders[NUM_EFFECTS];
    effect_params_t params = {};

    int currentEffect = 0;

    const char* getEffectName(int index);
};
