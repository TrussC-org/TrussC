#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    static constexpr int FFT_SIZE = 1024;
    vector<float> fftInput;
    vector<float> spectrum;
};
