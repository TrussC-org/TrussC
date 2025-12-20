#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;
#include "tc/sound/tcSound.h"
#include "tc/math/tcFFT.h"
#include <vector>
#include <complex>

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Sound music;
    bool musicLoaded = false;

    // FFT related
    static constexpr int FFT_SIZE = 1024;
    std::vector<float> fftInput;
    std::vector<float> spectrum;
    std::vector<float> spectrumSmooth;  // For smoothing

    // Visualization settings
    float smoothing = 0.8f;  // Smoothing coefficient
    bool useLogScale = true; // Logarithmic scale display
    bool showWaveform = true;
};
