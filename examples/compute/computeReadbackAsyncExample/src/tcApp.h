#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    StorageBuffer counter;
    ComputeShader compute;
    uint32_t count = 0;
public:
    void setup() override;
    void draw() override;
};
