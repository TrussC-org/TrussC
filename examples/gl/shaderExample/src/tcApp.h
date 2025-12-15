#pragma once

#include "TrussC.h"
#include "tcBaseApp.h"
#include "tc/gl/tcShader.h"

class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    tc::Shader shader;
    int currentEffect = 0;
    static constexpr int NUM_EFFECTS = 4;

    void loadEffect(int index);
};
