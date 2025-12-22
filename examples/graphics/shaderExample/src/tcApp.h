#pragma once

#include "TrussC.h"
#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include "tc/gpu/tcShader.h"

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Shader shader;
    int currentEffect = 0;
    static constexpr int NUM_EFFECTS = 4;

    void loadEffect(int index);
};
