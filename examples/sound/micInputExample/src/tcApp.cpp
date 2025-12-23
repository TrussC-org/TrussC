// =============================================================================
// micInputExample - Microphone Input FFT Spectrum Visualization
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    fftInput.resize(FFT_SIZE, 0.0f);
    spectrum.resize(FFT_SIZE / 2, 0.0f);
    spectrumSmooth.resize(FFT_SIZE / 2, 0.0f);

    // Start microphone input
    if (getMicInput().start()) {
        micStarted = true;
        tcLogNotice("tcApp") << "Microphone started!";
    } else {
        tcLogNotice("tcApp") << "Failed to start microphone.";
    }

    tcLogNotice("tcApp") << "=== Controls ===";
    tcLogNotice("tcApp") << "SPACE: Start/Stop mic";
    tcLogNotice("tcApp") << "W: Toggle waveform";
    tcLogNotice("tcApp") << "L: Toggle log scale";
    tcLogNotice("tcApp") << "UP/DOWN: Smoothing";
    tcLogNotice("tcApp") << "================";
}

void tcApp::update() {
    if (!micStarted || !getMicInput().isRunning()) return;

    // Get latest audio samples from microphone
    getMicAnalysisBuffer(fftInput.data(), FFT_SIZE);

    // Apply window function and execute FFT
    auto fftResult = fftReal(fftInput, WindowType::Hanning);

    // Calculate magnitude (with log scale support)
    for (size_t i = 0; i < spectrum.size(); i++) {
        float mag = std::abs(fftResult[i]);

        if (useLogScale) {
            // dB scale: map -60dB ~ 0dB to 0.0 ~ 1.0
            float db = (mag > 0) ? 20.0f * std::log10(mag) : -100.0f;
            spectrum[i] = (db + 60.0f) / 60.0f;  // Below -60dB is 0
            spectrum[i] = std::max(0.0f, std::min(1.0f, spectrum[i]));
        } else {
            // Linear scale (amplified)
            spectrum[i] = std::min(1.0f, mag * 4.0f);
        }
    }
}

void tcApp::draw() {
    clear(0.08f);

    float windowW = getWindowWidth();
    float windowH = getWindowHeight();

    // Title
    setColor(colors::white);
    drawBitmapString("TrussC Microphone FFT Analyzer", 20, 30);

    // Control instructions
    setColor(0.6f);
    drawBitmapString("SPACE:Start/Stop  W:Waveform  L:LogScale  UP/DOWN:Smoothing", 20, 50);

    // Status
    drawBitmapString(format("Status: {} | Smoothing: {:.0f}% | Scale: {}",
            getMicInput().isRunning() ? "Recording" : "Stopped",
            smoothing * 100,
            useLogScale ? "Log" : "Linear"), 20, 70);

    // Waveform display area
    if (showWaveform) {
        float waveY = 120;
        float waveH = 100;

        setColor(0.16f);
        drawRect(20, waveY, windowW - 40, waveH);

        setColor(colors::lime);
        drawBitmapString("Waveform (Mic Input)", 25, waveY + 15);

        // Draw waveform (actual audio data)
        setColor(colors::cyan);
        int waveWidth = (int)(windowW - 40);
        float prevX = 20, prevY = waveY + waveH / 2;

        for (int i = 0; i < waveWidth; i++) {
            // Map samples from fftInput
            int sampleIdx = (i * FFT_SIZE) / waveWidth;
            float sample = fftInput[sampleIdx];

            float x = 20 + i;
            float y = waveY + waveH / 2 - sample * waveH / 2;
            if (i > 0) {
                drawLine(prevX, prevY, x, y);
            }
            prevX = x;
            prevY = y;
        }
    }

    // Spectrum display area
    float specY = showWaveform ? 240 : 120;
    float specH = windowH - specY - 40;

    setColor(0.16f);
    drawRect(20, specY, windowW - 40, specH);

    setColor(colors::lime);
    drawBitmapString("Spectrum", 25, specY + 15);

    // Draw spectrum bars
    int numBars = 64;
    float barWidth = (windowW - 60) / numBars;
    float barGap = 2;

    // Group FFT results into numBars bars (show low frequencies in more detail)
    int spectrumSize = FFT_SIZE / 2;

    for (int i = 0; i < numBars; i++) {
        // Map bins with log scale (more detail for low frequencies)
        float ratio = (float)i / numBars;
        int binStart = (int)(ratio * ratio * spectrumSize);
        int binEnd = (int)(((float)(i + 1) / numBars) * ((float)(i + 1) / numBars) * spectrumSize);
        binEnd = std::max(binEnd, binStart + 1);
        binEnd = std::min(binEnd, spectrumSize);

        // Calculate average within range
        float value = 0.0f;
        for (int bin = binStart; bin < binEnd; bin++) {
            value += spectrum[bin];
        }
        value /= (binEnd - binStart);

        // Smoothing
        spectrumSmooth[i] = spectrumSmooth[i] * smoothing + value * (1.0f - smoothing);

        float barH = spectrumSmooth[i] * (specH - 30);
        float barX = 30 + i * barWidth;
        float barY = specY + specH - barH - 10;

        // Gradient color (HSB: blue -> green -> yellow)
        float hue = 0.6f - spectrumSmooth[i] * 0.4f;
        setColorHSB(hue, 0.8f, 0.9f);

        drawRect(barX, barY, barWidth - barGap, barH);
    }

    // Frequency labels
    setColor(0.4f);
    drawBitmapString("0 Hz", 30, specY + specH + 5);
    drawBitmapString("22050 Hz", windowW - 80, specY + specH + 5);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        if (getMicInput().isRunning()) {
            getMicInput().stop();
            tcLogNotice("tcApp") << "Microphone stopped";
        } else {
            getMicInput().start();
            tcLogNotice("tcApp") << "Microphone started";
        }
    }
    else if (key == 'w' || key == 'W') {
        showWaveform = !showWaveform;
        tcLogNotice("tcApp") << "Waveform: " << (showWaveform ? "ON" : "OFF");
    }
    else if (key == 'l' || key == 'L') {
        useLogScale = !useLogScale;
        tcLogNotice("tcApp") << "Log scale: " << (useLogScale ? "ON" : "OFF");
    }
    else if (key == SAPP_KEYCODE_UP) {
        smoothing = std::min(0.99f, smoothing + 0.05f);
        tcLogNotice("tcApp") << "Smoothing: " << (int)(smoothing * 100) << "%";
    }
    else if (key == SAPP_KEYCODE_DOWN) {
        smoothing = std::max(0.0f, smoothing - 0.05f);
        tcLogNotice("tcApp") << "Smoothing: " << (int)(smoothing * 100) << "%";
    }
}

