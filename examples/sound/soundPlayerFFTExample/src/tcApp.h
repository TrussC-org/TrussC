#pragma once

#include "tcBaseApp.h"
using namespace tc;
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

    // FFT 関連
    static constexpr int FFT_SIZE = 1024;
    std::vector<float> fftInput;
    std::vector<float> spectrum;
    std::vector<float> spectrumSmooth;  // スムージング用

    // 可視化設定
    float smoothing = 0.8f;  // スムージング係数
    bool useLogScale = true; // 対数スケール表示
    bool showWaveform = true;
};
