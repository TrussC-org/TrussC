#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    Texture output;
    ComputeShader compute;
    void setup() override;
    void update() override;
    void draw() override;
};
