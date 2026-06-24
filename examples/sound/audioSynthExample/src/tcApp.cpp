// =============================================================================
// audioSynthExample - Real-time audio synthesis via App::audioOut()
//
// Demonstrates the audioOut event listener path:
//   - Override App::audioOut() to fill the device buffer with sine samples.
//   - Touch frequency / amplitude from the UI thread (keyPressed) via
//     atomic<float>; the audio thread reads them in audioOut().
//   - phase is only ever touched on the audio thread, so it stays plain.
//
// Also shows:
//   - AudioSettings — opts into a 48 kHz / 256-frame buffer at startup.
//   - listDevices() — enumerate available playback devices.
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

void tcApp::setup() {
    setFps(VSYNC);

    // Optional: configure the engine before any Sound::load() / play() /
    // audioOut listener fires. init() defaults to 96 kHz; here we ask for
    // 48 kHz with a tighter buffer for lower latency. Comment this out to
    // use defaults.
    AudioSettings as;
    as.sampleRate = 48000;
    as.bufferSize = 256;
    AudioEngine::getInstance().init(as);

    // Capture device list for display.
    devices = AudioEngine::listDevices();

    logNotice("tcApp") << "Engine: "
        << AudioEngine::getInstance().getSampleRate() << " Hz, "
        << AudioEngine::getInstance().getChannels() << " ch";
    logNotice("tcApp") << "Available playback devices: " << devices.size();
    for (auto& d : devices) {
        logNotice("tcApp") << "  - " << d.name << (d.isDefault ? " (default)" : "");
    }

    logNotice("tcApp") << "=== Controls ===";
    logNotice("tcApp") << "UP/DOWN: Frequency (+/- 20 Hz)";
    logNotice("tcApp") << "LEFT/RIGHT: Amplitude (+/- 0.05)";
    logNotice("tcApp") << "SPACE: Mute / Unmute";
    logNotice("tcApp") << "================";
}

void tcApp::audioOut(AudioOutBuffer& buf) {
    // Runs on the audio thread. No engine API calls here, no allocations.
    if (muted.load()) return;

    const float freq = frequency.load();
    const float amp  = amplitude.load();
    const double dPhase = (TAU * (double)freq) / (double)buf.sampleRate;

    for (int i = 0; i < buf.frameCount; i++) {
        float v = amp * (float)std::sin(phase);
        for (int c = 0; c < buf.channels; c++) {
            buf.data[i * buf.channels + c] += v;
        }
        phase += dPhase;
        // Keep phase bounded so it doesn't lose float precision over time.
        if (phase >= TAU) phase -= TAU;
    }
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;
    setColor(colors::white);
    drawBitmapString("TrussC Audio Synth Example", 40, y); y += 32;

    setColor(0.7f);
    drawBitmapString(format("Engine: {} Hz, {} ch, {} voices",
            AudioEngine::getInstance().getSampleRate(),
            AudioEngine::getInstance().getChannels(),
            AudioEngine::getInstance().getMaxPolyphony()), 40, y); y += 22;

    drawBitmapString("---", 40, y); y += 16;

    drawBitmapString(format("Frequency: {:.1f} Hz", frequency.load()), 40, y); y += 22;
    drawBitmapString(format("Amplitude: {:.2f}", amplitude.load()), 40, y); y += 22;
    drawBitmapString(string("Muted: ") + (muted.load() ? "YES" : "no"), 40, y); y += 32;

    setColor(0.55f);
    drawBitmapString("UP/DOWN  - Frequency", 40, y); y += 18;
    drawBitmapString("LEFT/RIGHT - Amplitude", 40, y); y += 18;
    drawBitmapString("SPACE  - Mute/Unmute", 40, y); y += 32;

    setColor(colors::white);
    drawBitmapString("=== Playback Devices ===", 40, y); y += 22;
    setColor(0.65f);
    for (auto& d : devices) {
        drawBitmapString(string("  ") + d.name + (d.isDefault ? " (default)" : ""), 40, y);
        y += 18;
    }

    setColor(0.4f);
    drawBitmapString("FPS: " + to_string((int)getFrameRate()), 40, getHeight() - 30);
}

void tcApp::keyPressed(int key) {
    if (key == SAPP_KEYCODE_UP) {
        frequency = frequency.load() + 20.0f;
    } else if (key == SAPP_KEYCODE_DOWN) {
        float f = frequency.load() - 20.0f;
        if (f < 20.0f) f = 20.0f;
        frequency = f;
    } else if (key == SAPP_KEYCODE_RIGHT) {
        float a = amplitude.load() + 0.05f;
        if (a > 1.0f) a = 1.0f;
        amplitude = a;
    } else if (key == SAPP_KEYCODE_LEFT) {
        float a = amplitude.load() - 0.05f;
        if (a < 0.0f) a = 0.0f;
        amplitude = a;
    } else if (key == ' ') {
        muted = !muted.load();
    }
}
