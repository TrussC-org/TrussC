// =============================================================================
// micInputExample - Microphone FFT Spectrum Visualization
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    fftInput.resize(FFT_SIZE, 0.0f);
    spectrum.resize(FFT_SIZE / 2, 0.0f);
    getMicInput().start();
}

void tcApp::update() {
    if (!getMicInput().isRunning()) return;

    getMicAnalysisBuffer(fftInput.data(), FFT_SIZE);
    auto fftResult = fftReal(fftInput, WindowType::Hanning);

    for (size_t i = 0; i < spectrum.size(); i++) {
        float mag = std::abs(fftResult[i]) * 4.0f;
        spectrum[i] = mag > 1.0f ? 1.0f : mag;
    }
}

void tcApp::draw() {
    clear(0.1f);

    float w = getWindowWidth();
    float h = getWindowHeight();

    // Title and status
    setColor(1.0f);
    drawBitmapString("Microphone Input", 20, 30);
    setColor(0.6f);
    drawBitmapString(getMicInput().isRunning() ? "Recording" : "Stopped", 20, 50);
    drawBitmapString("SPACE: Start/Stop", 20, 70);

    // Waveform
    float waveY = 100;
    float waveH = (h - 140) / 2;

    setColor(0.4f, 0.8f, 0.4f);
    float prevX = 20, prevY = waveY + waveH / 2;
    for (int i = 0; i < (int)w - 40; i++) {
        int idx = i * FFT_SIZE / ((int)w - 40);
        float x = 20 + i;
        float y = waveY + waveH / 2 - fftInput[idx] * waveH / 2;
        if (i > 0) drawLine(prevX, prevY, x, y);
        prevX = x;
        prevY = y;
    }

    // Spectrum bars
    float specY = waveY + waveH + 20;
    float specH = h - specY - 20;
    int numBars = 64;
    float barW = (w - 40) / numBars;

    setColor(0.4f, 0.6f, 0.9f);
    for (int i = 0; i < numBars; i++) {
        int bin = i * (FFT_SIZE / 2) / numBars;
        float barH = spectrum[bin] * specH;
        drawRect(20 + i * barW, specY + specH - barH, barW - 2, barH);
    }
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        if (getMicInput().isRunning()) {
            getMicInput().stop();
        } else {
            getMicInput().start();
        }
    }
}
