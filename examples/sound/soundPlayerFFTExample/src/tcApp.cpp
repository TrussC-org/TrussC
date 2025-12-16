// =============================================================================
// soundPlayerFFTExample - FFT スペクトラム可視化
// =============================================================================
//
// Audio Credit:
// -----------------------------------------------------------------------------
// Track: "113 2b loose-pants 4.2 mono"
// Author: astro_denticle
// Source: https://freesound.org/people/astro_denticle/
// License: CC0 1.0 Universal (Public Domain)
//
// Thank you astro_denticle for releasing this awesome beat under CC0!
// -----------------------------------------------------------------------------

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    tc::setVsync(true);

    fftInput.resize(FFT_SIZE, 0.0f);
    spectrum.resize(FFT_SIZE / 2, 0.0f);
    spectrumSmooth.resize(FFT_SIZE / 2, 0.0f);

    // 音楽をロード
    std::string musicPath = "data/beat_loop.wav";
    if (music.load(musicPath)) {
        musicLoaded = true;
        music.setLoop(true);
        music.play();
        printf("Music loaded: %s (%.1f sec)\n", musicPath.c_str(), music.getDuration());
    } else {
        printf("Music not found: %s - using test tone\n", musicPath.c_str());
        music.loadTestTone(440.0f, 3.0f);
        music.setLoop(true);
        music.play();
        musicLoaded = true;
    }

    printf("\n=== Controls ===\n");
    printf("SPACE: Play/Stop\n");
    printf("W: Toggle waveform\n");
    printf("L: Toggle log scale\n");
    printf("UP/DOWN: Smoothing\n");
    printf("================\n\n");
}

void tcApp::update() {
    if (!musicLoaded || !music.isPlaying()) return;

    // AudioEngineから最新のオーディオサンプルを取得
    tc::getAudioAnalysisBuffer(fftInput.data(), FFT_SIZE);

    // 窓関数を適用してFFT実行
    auto fftResult = tc::fftReal(fftInput, tc::WindowType::Hanning);

    // マグニチュードを計算（対数スケール対応）
    for (size_t i = 0; i < spectrum.size(); i++) {
        float mag = std::abs(fftResult[i]);

        if (useLogScale) {
            // dBスケール: -60dB ~ 0dB を 0.0 ~ 1.0 にマップ
            float db = (mag > 0) ? 20.0f * std::log10(mag) : -100.0f;
            spectrum[i] = (db + 60.0f) / 60.0f;  // -60dB以下は0
            spectrum[i] = std::max(0.0f, std::min(1.0f, spectrum[i]));
        } else {
            // リニアスケール（増幅）
            spectrum[i] = std::min(1.0f, mag * 4.0f);
        }
    }
}

void tcApp::draw() {
    tc::clear(20);

    float windowW = tc::getWindowWidth();
    float windowH = tc::getWindowHeight();

    // タイトル
    tc::setColor(tc::colors::white);
    tc::drawBitmapString("TrussC FFT Spectrum Analyzer", 20, 30);

    // コントロール説明
    tc::setColor(150);
    tc::drawBitmapString("SPACE:Play/Stop  W:Waveform  L:LogScale  UP/DOWN:Smoothing", 20, 50);

    // ステータス
    char buf[128];
    snprintf(buf, sizeof(buf), "Status: %s | Smoothing: %.0f%% | Scale: %s",
            music.isPlaying() ? "Playing" : "Stopped",
            smoothing * 100,
            useLogScale ? "Log" : "Linear");
    tc::drawBitmapString(buf, 20, 70);

    // 波形表示エリア
    if (showWaveform) {
        float waveY = 120;
        float waveH = 100;

        tc::setColor(40);
        tc::drawRect(20, waveY, windowW - 40, waveH);

        tc::setColor(tc::colors::lime);
        tc::drawBitmapString("Waveform", 25, waveY + 15);

        // 波形を描画（実際のオーディオデータ）
        tc::setColor(tc::colors::cyan);
        int waveWidth = (int)(windowW - 40);
        float prevX = 20, prevY = waveY + waveH / 2;

        for (int i = 0; i < waveWidth; i++) {
            // fftInput からサンプルをマッピング
            int sampleIdx = (i * FFT_SIZE) / waveWidth;
            float sample = fftInput[sampleIdx];

            float x = 20 + i;
            float y = waveY + waveH / 2 - sample * waveH / 2;
            if (i > 0) {
                tc::drawLine(prevX, prevY, x, y);
            }
            prevX = x;
            prevY = y;
        }
    }

    // スペクトラム表示エリア
    float specY = showWaveform ? 240 : 120;
    float specH = windowH - specY - 80;

    tc::setColor(40);
    tc::drawRect(20, specY, windowW - 40, specH);

    tc::setColor(tc::colors::lime);
    tc::drawBitmapString("Spectrum", 25, specY + 15);

    // スペクトラムバーを描画
    int numBars = 64;
    float barWidth = (windowW - 60) / numBars;
    float barGap = 2;

    // FFT結果を numBars 本にまとめる（低周波をより細かく表示）
    int spectrumSize = FFT_SIZE / 2;

    for (int i = 0; i < numBars; i++) {
        // 対数スケールでビンをマッピング（低周波を細かく）
        float ratio = (float)i / numBars;
        int binStart = (int)(ratio * ratio * spectrumSize);
        int binEnd = (int)(((float)(i + 1) / numBars) * ((float)(i + 1) / numBars) * spectrumSize);
        binEnd = std::max(binEnd, binStart + 1);
        binEnd = std::min(binEnd, spectrumSize);

        // 範囲内の平均値を計算
        float value = 0.0f;
        for (int bin = binStart; bin < binEnd; bin++) {
            value += spectrum[bin];
        }
        value /= (binEnd - binStart);

        // スムージング
        spectrumSmooth[i] = spectrumSmooth[i] * smoothing + value * (1.0f - smoothing);

        float barH = spectrumSmooth[i] * (specH - 30);
        float barX = 30 + i * barWidth;
        float barY = specY + specH - barH - 10;

        // グラデーションカラー（HSB: 青→緑→黄）
        float hue = 0.6f - spectrumSmooth[i] * 0.4f;
        tc::setColorHSB(hue, 0.8f, 0.9f);

        tc::drawRect(barX, barY, barWidth - barGap, barH);
    }

    // 周波数ラベル
    tc::setColor(100);
    tc::drawBitmapString("0 Hz", 30, specY + specH + 5);
    tc::drawBitmapString("22050 Hz", windowW - 80, specY + specH + 5);

    // クレジット
    tc::setColor(80);
    tc::drawBitmapString("Audio: \"113 2b loose-pants 4.2 mono\" by astro_denticle (CC0)", 20, windowH - 25);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        if (music.isPlaying()) {
            music.stop();
            printf("Music stopped\n");
        } else {
            music.play();
            printf("Music playing\n");
        }
    }
    else if (key == 'w' || key == 'W') {
        showWaveform = !showWaveform;
        printf("Waveform: %s\n", showWaveform ? "ON" : "OFF");
    }
    else if (key == 'l' || key == 'L') {
        useLogScale = !useLogScale;
        printf("Log scale: %s\n", useLogScale ? "ON" : "OFF");
    }
    else if (key == SAPP_KEYCODE_UP) {
        smoothing = std::min(0.99f, smoothing + 0.05f);
        printf("Smoothing: %.0f%%\n", smoothing * 100);
    }
    else if (key == SAPP_KEYCODE_DOWN) {
        smoothing = std::max(0.0f, smoothing - 0.05f);
        printf("Smoothing: %.0f%%\n", smoothing * 100);
    }
}
