#include <TrussC.h>
#include "tc/sound/tcAudioDeviceInternal.h"
#include "tc/sound/tcMicInputInternal.h"
#include "../../common/tcCoreTest.h"
#include <cstdio>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
void check(const char* label, bool ok) {
    printf("%-76s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
#ifndef __EMSCRIPTEN__
void mappingTests() {
    const pair<AudioBackend, ma_backend> mappings[] = {
        {AudioBackend::Null, ma_backend_null}, {AudioBackend::WASAPI, ma_backend_wasapi},
        {AudioBackend::DirectSound, ma_backend_dsound}, {AudioBackend::WinMM, ma_backend_winmm},
        {AudioBackend::CoreAudio, ma_backend_coreaudio}, {AudioBackend::PulseAudio, ma_backend_pulseaudio},
        {AudioBackend::ALSA, ma_backend_alsa}, {AudioBackend::JACK, ma_backend_jack},
        {AudioBackend::AAudio, ma_backend_aaudio}, {AudioBackend::OpenSLES, ma_backend_opensl},
        {AudioBackend::WebAudio, ma_backend_webaudio}
    };
    for (const auto& [api, native] : mappings)
        check("explicit backend maps in both directions", internal::audioBackendToMA(api) == native &&
            internal::audioBackendFromMA(native) == api);
    ma_device_info info{};
    strcpy(info.name, "Capture");
    strcpy(info.id.alsa, "hw:2,0");
    info.isDefault = MA_TRUE;
    info.nativeDataFormatCount = 4;
    info.nativeDataFormats[0].channels = 2; info.nativeDataFormats[0].sampleRate = 48000;
    info.nativeDataFormats[1].channels = 8; info.nativeDataFormats[1].sampleRate = 44100;
    info.nativeDataFormats[2].channels = 1; info.nativeDataFormats[2].sampleRate = 48000;
    const auto converted = internal::audioDeviceInfo(ma_backend_alsa, info);
    check("native format union preserves max channels and distinct reported rates",
        converted.name == "Capture" && converted.uniqueId == "hw:2,0" && converted.isDefault &&
        converted.backend == AudioBackend::ALSA && converted.channels == 8 &&
        converted.sampleRates == vector<int>({44100, 48000}));
    strcpy(info.id.pulse, "sink.monitor");
    check("Pulse ID is passed through without filtering monitors",
        internal::audioDeviceUniqueId(ma_backend_pulseaudio, info.id) == "sink.monitor");
    strcpy(info.id.coreaudio, "core-audio-uid");
    check("Core Audio ID is the native UID", internal::audioDeviceUniqueId(ma_backend_coreaudio, info.id) == "core-audio-uid");
    info.id.winmm = 7;
    check("WinMM ID is its native index", internal::audioDeviceUniqueId(ma_backend_winmm, info.id) == "7");
    memset(&info.id, 0, sizeof info.id); info.id.dsound[0] = 0xab; info.id.dsound[15] = 0xcd;
    check("DirectSound ID encodes GUID bytes only", internal::audioDeviceUniqueId(ma_backend_dsound, info.id) == "ab0000000000000000000000000000cd");
}
#endif
}

TC_CORE_TEST_MAIN() {
#ifdef __EMSCRIPTEN__
    check("Web playback enumeration is empty", AudioEngine::listDevices().empty());
    check("Web capture enumeration is empty", MicInput::listDevices().empty());
#else
    getMainThreadId();
    mappingTests();
    auto& engine = AudioEngine::getInstance();
    const auto outputs = AudioEngine::listDevices(AudioBackend::Null);
    const auto inputs = MicInput::listDevices(AudioBackend::Null);
    check("enumeration does not start output", !engine.isInitialized());
    check("output list contains playback only", outputs.size() == 1 && outputs[0].name == "NULL Playback Device");
    check("input list contains capture only", inputs.size() == 1 && inputs[0].name == "NULL Capture Device");
    if (outputs.empty() || inputs.empty()) return 1;
    check("Null listing shape reports backend ID/default and unspecified capabilities",
        outputs[0].backend == AudioBackend::Null && outputs[0].uniqueId == "0" && outputs[0].isDefault &&
        outputs[0].channels == 0 && outputs[0].sampleRates.empty());
    AudioSettings settings;
    settings.device = outputs[0];
    settings.device->name = "stale name";
    settings.deviceName = "absent name";
    settings.backend = AudioBackend::WASAPI;
    check("output object selects by ID and owns backend, ahead of name", engine.init(settings));
    check("default listing follows current engine context", AudioEngine::listDevices()[0].backend == AudioBackend::Null);

    atomic<int> missingWarnings{0};
    atomic<int> errors{0};
    bool requestedNameLogged = false;
    auto warning = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.level == LogLevel::Warning && args.message.find("not found") != string::npos) ++missingWarnings;
        if (args.level == LogLevel::Error) ++errors;
        if (args.level == LogLevel::Notice && args.message.find("re-initializing (") != string::npos)
            requestedNameLogged = args.message.find("dev='stale name'") != string::npos;
    });
    settings.device->uniqueId = "missing-id";
    check("missing output ID fails instead of opening default", !engine.init(settings) && !engine.isInitialized());
    check("reinit notice uses requested device object's name", requestedNameLogged);
    check("missing output ID warns", missingWarnings == 1);
    settings.device.reset(); settings.backend = AudioBackend::Null;
    check("missing output name also fails without fallback", !engine.init(settings) && !engine.isInitialized());
    check("missing output name warns", missingWarnings == 2);

    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::Authorized; }, {});
    MicInput mic;
    auto input = inputs[0]; input.name = "stale name";
    mic.setDevice(input);
    check("mic object selects Null by ID without output", mic.start() && mic.getDeviceName() == "NULL Capture Device");
    mic.stop();
    check("setup retains selected device", mic.setup());
    mic.stop();
    input.uniqueId = "missing-id";
    mic.setDevice(input);
    check("missing capture ID fails without fallback", !mic.start() && !mic.isRunning());
    check("missing capture ID warns", missingWarnings == 3);
    check("missing capture ID emits no error log", errors == 0);
    mic.stop(); internal::waitForResourceLoadsForTests();

    // Exercise real async startup and main-thread failure delivery, without hardware.
    internal::appRunToken() = make_shared<atomic<bool>>(true);
    int events = 0;
    auto result = mic.onStart.listen([&](MicStartResult& args) {
        ++events;
        check("async missing-device result arrives on main with no fallback",
            !args.success && args.message == "Selected capture device not found" &&
            isMainThread() && !mic.isRunning() && !mic.isStarting());
    });
    mic.startAsync();
    internal::waitForResourceLoadsForTests();
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    check("async missing capture ID reports exactly one failure and warning", events == 1 && missingWarnings == 4);
    check("async missing capture ID emits no error log", errors == 0);
    mic.stop(); internal::waitForResourceLoadsForTests();
    internal::appRunToken()->store(false);
    internal::appRunToken().reset();
#ifdef __linux__
    settings.deviceName.clear(); settings.backend = AudioBackend::WASAPI;
    check("unavailable explicit backend fails without fallback", !engine.init(settings) && !engine.isInitialized());
    check("unavailable explicit backend enumeration is empty", AudioEngine::listDevices(AudioBackend::WASAPI).empty());
    input.backend = AudioBackend::WASAPI;
    mic.setDevice(input);
    check("unavailable selected capture backend fails without fallback", !mic.start() && !mic.isRunning());
    mic.stop(); internal::waitForResourceLoadsForTests();
#endif
    engine.shutdown();
    internal::setMicAuthorizationForTests({}, {});
#endif
    return failures ? 1 : 0;
}
