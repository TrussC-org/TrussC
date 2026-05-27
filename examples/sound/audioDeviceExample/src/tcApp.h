#pragma once

#include <TrussC.h>
#include <tcxImGui.h>
#include <atomic>
using namespace std;
using namespace tc;
using namespace tcx;

// Live audio device + sample rate switching demo. A sine wave plays
// continuously through App::audioOut(); the ImGui panel lets the user
// pick the playback device and sample rate, applying immediately via
// AudioEngine::init({...}). The sine survives each re-init (short
// audible gap during the device cycle).
class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void cleanup() override;

    // Real-time sine synth — runs on the audio thread.
    void audioOut(AudioOutBuffer& buf) override;

private:
    // Synth parameters (touched from both UI and audio threads).
    std::atomic<float> frequency{440.0f};
    std::atomic<float> amplitude{0.3f};

    // Phase — audio-thread-only.
    double phase = 0.0;

    // Devices snapshot (refreshed after every audioDeviceChanged event).
    std::vector<AudioDeviceInfo> devices;
    int selectedDeviceIdx = 0;       // index into `devices`
    int selectedSampleRateIdx = 0;   // index into kSampleRates

    // Available sample rates the UI lets the user pick.
    static constexpr int kSampleRates[3] = {44100, 48000, 96000};

    // Last init status (shown in UI).
    std::string lastInitMessage;
    bool lastInitOk = true;

    // Listener for AudioEngine::audioDeviceChanged. Fires on initial
    // init AND every successful re-init.
    EventListener audioDeviceListener;
    std::string lastDeviceEventMessage;

    // Refresh devices list and reset selected index based on current engine.
    void refreshDevices();

    // Apply the currently-selected device + rate. Updates lastInitMessage.
    void applyCurrentSelection();
};
