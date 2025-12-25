#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// fboExample - FBO (Frame Buffer Object) sample
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Fbo fbo_;
};
