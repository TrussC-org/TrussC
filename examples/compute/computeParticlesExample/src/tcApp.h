#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    static constexpr int count = 2048;
    StorageBuffer particles;
    ComputeShader step;
    Shader points;
public:
    void setup() override;
    void update() override;
    void draw() override;
};
