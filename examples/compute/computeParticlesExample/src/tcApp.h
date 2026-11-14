#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    // keep this a multiple of 64: update() dispatches count / 64 groups
    // of 64 threads, so any remainder would never move
    static constexpr int count = 1024*128;

    // GPU-side particle array, one Vec4 per particle:
    // xy = position in clip space (-1..1), zw = velocity per second
    StorageBuffer particles;

    // compute pass: moves every particle one frame forward
    ComputeShader step;

    // draw pass: reads the same buffer and draws a small triangle per particle
    Shader points;

    // set by a click, sent to the compute shader on the next update()
    float kick = 0;

public:
    void setup() override;
    void update() override;
    void draw() override;
    void mousePressed(const MouseEventArgs& e) override;
};
