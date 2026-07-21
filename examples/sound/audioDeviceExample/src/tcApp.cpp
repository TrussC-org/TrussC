// =============================================================================
// audioDeviceExample - Live audio device + sample rate switching
//
// A sine wave runs continuously through App::audioOut(). The ImGui panel
// lets the user pick a playback device and sample rate; each change calls
// AudioEngine::init({...}), which tears down the device and restarts at
// the new settings. Active voices preserve their playback position across
// the swap (the sine just keeps phasing on).
//
// The audioDeviceChanged event fires after each successful init() — the
// panel shows the latest event payload so you can see the resolved
// device name (which may differ from the requested one if a fallback
// happened) and whether it's the system default.
// =============================================================================

#include "tcApp.h"
#include "TrussC.h"

constexpr int tcApp::kSampleRates[3];

void tcApp::setup() {
    setFps(VSYNC);

    // Hook the engine's device-changed event BEFORE init() so we also
    // observe the initial init's fire (not just subsequent re-inits).
    audioDeviceListener = AudioEngine::getInstance().audioDeviceChanged.listen(
        [this](AudioDeviceChangedArgs& a) {
            lastDeviceEventMessage = format(
                "device='{}' default={} {}Hz {}ch buf={} poly={}",
                a.deviceName, a.isDefaultDevice ? "yes" : "no",
                a.sampleRate, a.channels, a.bufferSize, a.maxPolyphony);
            logNotice("audioDeviceChanged") << lastDeviceEventMessage;
        });

    // Start with system defaults — known-good initial state.
    AudioEngine::getInstance().init();

    imguiSetup();
    refreshDevices();

    // Enable MCP debugger + ImGui tools so this example can be driven
    // automatically (e.g. for stress testing via mcp:tcx_imgui_click).
    // Only active when TRUSSC_MCP=1 is set in the environment.
    // MCP is desktop-only — Web builds skip it.
#ifndef __EMSCRIPTEN__
    mcp::registerControlTools();
    imgui_tools::registerImGuiTools();
#endif

    logNotice("tcApp") << "Engine: "
        << AudioEngine::getInstance().getSampleRate() << " Hz, "
        << AudioEngine::getInstance().getChannels() << " ch";
    logNotice("tcApp") << "Available playback devices: " << devices.size();
    for (auto& d : devices) {
        logNotice("tcApp") << "  - " << d.name << (d.isDefault ? " (default)" : "");
    }
}

void tcApp::cleanup() {
    // ImGui must be torn down before sokol_gfx shuts down, otherwise
    // ImGui's GPU resources get freed in the wrong order and the MSVC
    // runtime aborts on Windows. App's cleanup() runs before the static
    // teardown that calls sg_shutdown, so this is the safe spot.
    imguiShutdown();
}

void tcApp::audioOut(AudioOutBuffer& buf) {
    const float freq = frequency.load();
    const float amp  = amplitude.load();
    const double dPhase = (TAU * (double)freq) / (double)buf.sampleRate;

    for (int i = 0; i < buf.frameCount; i++) {
        float v = amp * (float)std::sin(phase);
        for (int c = 0; c < buf.channels; c++) {
            buf.data[i * buf.channels + c] += v;
        }
        phase += dPhase;
        if (phase >= TAU) phase -= TAU;
    }
}

void tcApp::refreshDevices() {
    devices = AudioEngine::listDevices();

    // Keep the previously-selected device if it's still present.
    int defaultIdx = 0;
    for (int i = 0; i < (int)devices.size(); ++i) {
        if (devices[i].isDefault) { defaultIdx = i; break; }
    }
    if (selectedDeviceIdx < 0 || selectedDeviceIdx >= (int)devices.size()) {
        selectedDeviceIdx = defaultIdx;
    }

    // Snap the rate picker to the engine's current rate if it matches a preset.
    int curRate = AudioEngine::getInstance().getSampleRate();
    for (int i = 0; i < 3; ++i) {
        if (kSampleRates[i] == curRate) { selectedSampleRateIdx = i; break; }
    }
}

void tcApp::applyCurrentSelection() {
    if (selectedDeviceIdx < 0 || selectedDeviceIdx >= (int)devices.size()) {
        lastInitOk = false;
        lastInitMessage = "No device selected";
        return;
    }
    AudioSettings as;
    as.sampleRate = kSampleRates[selectedSampleRateIdx];
    as.deviceName = devices[selectedDeviceIdx].name;
    bool ok = AudioEngine::getInstance().init(as);
    lastInitOk = ok;
    lastInitMessage = ok
        ? (std::string("OK: ") + as.deviceName + " @ " + std::to_string(as.sampleRate) + " Hz")
        : (std::string("FAILED: ") + as.deviceName + " @ " + std::to_string(as.sampleRate) + " Hz");

    logNotice("tcApp") << lastInitMessage;
}

void tcApp::draw() {
    clear(0.12f);

    // Plain text so the window still has something to look at when ImGui is hidden.
    setColor(colors::white);
    drawBitmapString(format("Engine: {} Hz, {} ch",
        AudioEngine::getInstance().getSampleRate(),
        AudioEngine::getInstance().getChannels()), 40, 40);

    setColor(0.55f);
    drawBitmapString(format("Sine: {:.0f} Hz @ amp {:.2f}",
        frequency.load(), amplitude.load()), 40, 62);

    // ----- ImGui panel -----
    imguiBegin();

    ImGui::SetNextWindowPos(ImVec2(10, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 440), ImGuiCond_FirstUseEver);
    ImGui::Begin("Audio Device Switcher");

    // Synth controls
    float freq = frequency.load();
    if (ImGui::SliderFloat("Frequency (Hz)", &freq, 20.0f, 2000.0f, "%.1f")) {
        frequency = freq;
    }
    float amp = amplitude.load();
    if (ImGui::SliderFloat("Amplitude", &amp, 0.0f, 1.0f, "%.2f")) {
        amplitude = amp;
    }

    ImGui::Separator();

    // Sample rate
    ImGui::Text("Sample Rate");
    for (int i = 0; i < 3; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "%d Hz", kSampleRates[i]);
        if (ImGui::RadioButton(label, selectedSampleRateIdx == i)) {
            selectedSampleRateIdx = i;
            applyCurrentSelection();
        }
        if (i < 2) ImGui::SameLine();
    }

    ImGui::Separator();

    // Devices
    ImGui::Text("Playback Device");
    if (ImGui::Button("Refresh device list")) {
        refreshDevices();
    }
    ImGui::BeginChild("devices", ImVec2(0, 180), true);
    for (int i = 0; i < (int)devices.size(); ++i) {
        std::string label = devices[i].name;
        if (devices[i].isDefault) label += " (default)";
        if (ImGui::Selectable(label.c_str(), selectedDeviceIdx == i)) {
            selectedDeviceIdx = i;
            applyCurrentSelection();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(lastInitOk ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                   : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "%s", lastInitMessage.c_str());

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f),
                       "Last audioDeviceChanged:");
    ImGui::TextWrapped("%s", lastDeviceEventMessage.empty()
                              ? "(not fired yet)"
                              : lastDeviceEventMessage.c_str());

    ImGui::End();
    imguiEnd();

    setColor(0.4f);
    drawBitmapString("FPS: " + to_string((int)getFrameRate()), 40, getHeight() - 30);
}
