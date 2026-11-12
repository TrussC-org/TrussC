#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    // one uint on the GPU; the compute shader adds to it
    StorageBuffer counter;

    // adds 1..100 into counter, one number per thread
    ComputeShader compute;

    // CPU copy of the result, filled by read()
    uint32_t count = 0;

public:
    void setup() override;
    void draw() override;
};
