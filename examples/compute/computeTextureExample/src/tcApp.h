#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    // 256x256 image the compute shader writes into
    Texture output;

    // fills output with a gradient
    ComputeShader compute;

    void setup() override;
    void update() override;
    void draw() override;
};
