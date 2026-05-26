#pragma once

#include <TrussC.h>
#include <atomic>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

    // Runs on the audio thread. Synthesize sine wave samples and ADD to
    // the buffer (other Sound voices, if any, have already been mixed in).
    void audioOut(AudioOutBuffer& buf) override;

private:
    // Synth parameters — touched from both the UI thread (keyPressed)
    // and the audio thread (audioOut). atomic<float> for safe hand-off.
    std::atomic<float> frequency{440.0f};
    std::atomic<float> amplitude{0.2f};
    std::atomic<bool>  muted{false};

    // Phase state — only the audio thread reads/writes it.
    double phase = 0.0;

    // Devices list captured at setup() for display.
    std::vector<AudioDeviceInfo> devices;
};
