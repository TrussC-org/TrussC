#pragma once

#include <TrussC.h>
#include <atomic>
using namespace std;
using namespace tc;

// =============================================================================
// audioRecorderExample - Record the master mix to a WAV file
//
// A sine synth plays through App::audioOut(); tc::AudioRecorder taps the
// engine's master output (priority::Monitor, i.e. AFTER the synth listener)
// and writes exactly what the speakers get to a WAV file. Press R to
// start / stop; the first 4 seconds are recorded automatically so the
// example produces a file even with no interaction.
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

    void audioOut(AudioOutBuffer& buf) override;

private:
    // Synth parameters (UI thread writes, audio thread reads).
    std::atomic<float> frequency{440.0f};
    std::atomic<float> amplitude{0.25f};

    double phase = 0.0;         // audio-thread only

    AudioRecorder recorder;
    AudioRecorder monoRecorder;   // channelMap + F32 variant (see setup)
    float autoStopAt = 4.0f;      // auto-record window at startup (0 = done)
};
