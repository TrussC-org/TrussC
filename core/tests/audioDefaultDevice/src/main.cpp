// #303: default-device reporting without an explicit selection ID, and by name.
#include <TrussC.h>
#include "tc/sound/tcAudioDeviceInternal.h"
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace std;
using namespace tc;

namespace {

int g_fail = 0;

void check(const string& name, bool ok) {
    printf("%-72s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    if (!ok) ++g_fail;
}

void checkSelection() {
    // PulseAudio enumerates nonzero string IDs but leaves playback.id zeroed
    // on an implicit default open. Null backend IDs would hide this mismatch.
    ma_device_info infos[2] = {};
    strcpy(infos[0].id.pulse, "alsa_output.pci-0000_00_1f.3.analog-stereo");
    infos[0].isDefault = MA_TRUE;
    strcpy(infos[1].id.pulse, "alsa_output.usb-secondary.analog-stereo");
    infos[1].isDefault = MA_FALSE;
    ma_device_id zeroID = {};
    check("PulseAudio fixture: default ID differs from zeroed playback ID",
          memcmp(&infos[0].id, &zeroID, sizeof(zeroID)) != 0);
    check("no selection ID: default with PulseAudio-style enumeration",
          internal::openedDeviceIsDefault(nullptr, infos, 2));
    check("no selection ID: default even without enumeration",
          internal::openedDeviceIsDefault(nullptr, nullptr, 0));
    check("named default: uses enumeration's true flag",
          internal::openedDeviceIsDefault(&infos[0].id, infos, 2));
    check("named secondary: uses enumeration's false flag",
          !internal::openedDeviceIsDefault(&infos[1].id, infos, 2));
    check("unmatched selection ID: not reported as default",
          !internal::openedDeviceIsDefault(&zeroID, infos, 2));
    check("named selection without enumeration: not reported as default",
          !internal::openedDeviceIsDefault(&infos[0].id, nullptr, 0));
    infos[0].isDefault = MA_FALSE;
    infos[1].isDefault = MA_TRUE;
    check("named former default: follows the changed enumeration flag",
          !internal::openedDeviceIsDefault(&infos[0].id, infos, 2));
    check("named new default: follows the changed enumeration flag",
          internal::openedDeviceIsDefault(&infos[1].id, infos, 2));
}

Json audioState() {
    const string reply = mcp::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"tc_get_audio_state","arguments":{"devices":false}}})");
    try {
        return Json::parse(Json::parse(reply).at("result").at("content").at(0)
                               .at("text").get<string>());
    } catch (...) {
        return Json();
    }
}

void checkInit(const string& label, const string& deviceName) {
    auto& engine = AudioEngine::getInstance();
    int events = 0;
    AudioDeviceChangedArgs changed;
    EventListener listener = engine.audioDeviceChanged.listen([&](AudioDeviceChangedArgs& args) {
        ++events;
        changed = args;
    });
    AudioSettings settings;
    settings.deviceName = deviceName;
    const bool started = engine.init(settings);
    check(label + ": init succeeds", started && engine.isInitialized());
    if (!started) return;
    check(label + ": event reports default", events == 1 && changed.isDefaultDevice);
    const auto report = internal::audioDeviceReport(false);
    check(label + ": device report agrees", report.backend == "Null" && report.outputIsDefault);
    const Json state = audioState();
    check(label + ": tc_get_audio_state output.default is true",
          state.is_object() && state.value("running", false) && state.contains("output") &&
          state["output"].value("default", false));
}

} // namespace

TC_CORE_TEST_MAIN() {
    checkSelection();
    internal::setNullAudioBackendForTests(true);
    mcp::registerInspectionTools();
    checkInit("implicit default", "");
    checkInit("unknown name falls back to default", "tc-audio-default-device-missing");
    const auto devices = AudioEngine::listDevices();
    string defaultName;
    for (const auto& device : devices) {
        if (device.isDefault) {
            defaultName = device.name;
            break;
        }
    }
    check("null backend enumerates a named default", !defaultName.empty());
    if (!defaultName.empty()) checkInit("named default", defaultName);
    AudioEngine::getInstance().shutdown();
    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
