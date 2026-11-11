// #303: default-device reporting without an explicit selection ID, and by name.
#include <TrussC.h>
#include "tc/sound/tcAudioDeviceInternal.h"
#include "../../common/tcCoreTest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
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
    const string reply = mcp::internal::Server::instance().processMessage(
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
    settings.backend = AudioBackend::Null;
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

void checkBackendFailure() {
    auto& engine = AudioEngine::getInstance();
    const auto before = internal::audioDeviceReport(true);
    check("default settings select Default", AudioSettings{}.backend == AudioBackend::Default);
    check("enumeration does not start the engine", !engine.isInitialized());
    check("initial stats have no failure", engine.getStats().initFailure == AudioInitFailure::None);

    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    settings.bufferSize = 256;
    settings.deviceName = "NULL Playback Device";
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("injected device-open failure stops init", !engine.init(settings) && !engine.isInitialized());
    const auto failed = engine.getStats();
    check("failure stats preserve reason/result/backend",
          failed.initFailure == AudioInitFailure::DeviceOpen &&
          failed.initFailureResult == MA_FAILED_TO_OPEN_BACKEND_DEVICE &&
          string(failed.initFailureBackend) == "Null");
    const auto report = internal::audioDeviceReport(false);
    check("failed init retains context and requested name",
          report.backend == "Null" && report.outputDevice.empty() &&
          report.initFailureDevice == settings.deviceName);
    const auto state = audioState();
    check("MCP reports stopped engine with exact failure",
          state.is_object() && !state.value("running", true) &&
          state.value("initFailure", Json()) == Json({{"reason", "deviceOpen"},
              {"result", MA_FAILED_TO_OPEN_BACKEND_DEVICE}, {"backend", "Null"},
              {"device", settings.deviceName}}));
    engine.shutdown();
    check("shutdown retains the last failure", engine.getStats().initFailure == AudioInitFailure::DeviceOpen);

    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);
    mutex callbackMutex;
    condition_variable callbackCv;
    bool called = false;
    EventListener listener = engine.audioOut.listen([&](AudioOutBuffer&) {
        lock_guard<mutex> lock(callbackMutex);
        called = true;
        callbackCv.notify_one();
    });
    settings.deviceName.clear();
    check("retry starts the Null backend", engine.init(settings));
    {
        unique_lock<mutex> lock(callbackMutex);
        check("Null drives audioOut", callbackCv.wait_for(lock, chrono::seconds(5), [&] { return called; }));
    }
    const auto recovered = engine.getStats();
    check("successful init clears every failure field",
          recovered.initFailure == AudioInitFailure::None && recovered.initFailureResult == 0 &&
          string(recovered.initFailureBackend).empty() &&
          internal::audioDeviceReport(false).initFailureDevice.empty());
    const auto running = audioState();
    check("MCP reports Null running without initFailure", running.is_object() &&
          running.value("running", false) && !running.contains("initFailure") &&
          running.at("output").value("backend", "") == "Null");
    const auto devices = AudioEngine::listDevices();
    check("enumeration follows the engine context", devices.size() == 1 &&
          devices[0].name == "NULL Playback Device");

    // Keep a diagnostics reader active across failed live re-init and recovery.
    atomic<bool> reading{true};
    atomic<bool> consistent{true};
    thread reader([&] {
        while (reading.load()) {
            const auto stats = engine.getStats();
            if (stats.initFailure == AudioInitFailure::DeviceOpen &&
                (stats.initFailureResult != MA_FAILED_TO_OPEN_BACKEND_DEVICE ||
                 string(stats.initFailureBackend) != "Null")) consistent.store(false);
        }
    });
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("failed live re-init stops the old device", !engine.init(settings) && !engine.isInitialized());
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);
    check("live re-init failure can recover", engine.init(settings));
    reading.store(false);
    reader.join();
    check("concurrent stats reads keep failure fields consistent", consistent.load());

    // Zero-argument init must discard Null selection, even after a failed init.
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("zero-argument init uses Default and surfaces its failure", !engine.init());
    const auto defaultReport = internal::audioDeviceReport(false);
    check("Default uses the same backend as the initial real-backend probe",
          defaultReport.backend == before.backend);
    check("device-open failure does not switch backend",
          engine.getStats().initFailure == AudioInitFailure::DeviceOpen &&
          string(engine.getStats().initFailureBackend) == before.backend);
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);
    check("explicit Null can replace the Default context", engine.init(settings));
    listener.disconnect();
    engine.shutdown();
}

template<class Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while (!predicate()) {
        if (chrono::steady_clock::now() >= deadline) return false;
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return true;
}

// Stops the open device behind the engine's back, from another thread, as the
// backend's own thread does when the OS takes the device away.
bool loseDevice(AudioEngine& engine) {
    bool stopped = false;
    thread([&] { stopped = internal::AudioDeviceLoss::injectStopForTests(); }).join();
    return stopped && waitFor([&] { return !engine.isConnected(); });
}

// #415: device lifecycle (docs/architecture/DEVICE_LIFECYCLE.md) of the engine.
void checkLifecycle() {
    auto& engine = AudioEngine::getInstance();
    engine.shutdown();
    int lost = 0;
    bool firedOnMain = false;
    bool connectedInListener = true;
    string reason;
    EventListener listener = engine.onDisconnect.listen([&](DeviceDisconnectEventArgs& e) {
        ++lost;
        reason = e.reason;
        firedOnMain = isMainThread();
        connectedInListener = engine.isConnected();
    });

    // Rule 1: isConnected() is true only while the device is open
    check("lifecycle: stopped engine is not connected", !engine.isConnected() && !engine.isInitialized());
    // Opened by name: a loss releases the device (the default-opened case
    // is checked at the end)
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    settings.sampleRate = 44100;
    settings.channels = 1;
    settings.deviceName = "NULL Playback Device";
    check("lifecycle: setup(settings) opens the device", engine.setup(settings) && engine.isConnected());
    check("lifecycle: isInitialized() is the same state", engine.isInitialized());

    // Rule 4: the device stops by itself
    check("lifecycle: a device stop clears isConnected()", loseDevice(engine));
    check("lifecycle: nothing fires before the main thread's turn", lost == 0);
    internal::drainMainThreadQueue();
    check("lifecycle: onDisconnect fires once, on the main thread", lost == 1 && firedOnMain);
    check("lifecycle: isConnected() is already false in the listener", !connectedInListener);
    check("lifecycle: the reason says what happened", reason.find("stopped") != string::npos);
    internal::drainMainThreadQueue();
    check("lifecycle: it does not fire again", lost == 1 && !engine.isConnected());
    check("lifecycle: the delivered loss released the device", !internal::AudioDeviceLoss::injectStopForTests());

    // Rule 2: setup() is init(): the system default device (the backend and
    // device name of setup(settings) are not kept) with the last rate and
    // channels. The fault keeps the test off real hardware.
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("lifecycle: setup() requests the default device (fault injected)", !engine.setup());
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);
    check("lifecycle: setup() keeps rate and channels",
          engine.getSampleRate() == 44100 && engine.getChannels() == 1);
    check("lifecycle: setup(settings) reopens", engine.setup(settings) && engine.isConnected());
    internal::drainMainThreadQueue();
    check("lifecycle: reopening does not fire", lost == 1);

    // Rule 3: close() closes and never fires
    engine.close();
    internal::drainMainThreadQueue();
    check("lifecycle: close() disconnects without onDisconnect", !engine.isConnected() && lost == 1);
    check("lifecycle: setup(settings) reopens after close()", engine.setup(settings) && engine.isConnected());
    check("lifecycle: a live re-init does not fire", engine.setup(settings) && engine.isConnected());
    engine.shutdown();
    internal::drainMainThreadQueue();
    check("lifecycle: shutdown() does not fire either", lost == 1);

    // A loss that was not delivered yet is dropped when the app has already
    // reopened or closed the device: isConnected() is true again, so an event
    // would break rule 4.
    check("lifecycle: reopen for the pending-loss cases", engine.setup(settings));
    check("lifecycle: second loss", loseDevice(engine));
    check("lifecycle: setup(settings) before delivery reconnects", engine.setup(settings) && engine.isConnected());
    internal::drainMainThreadQueue();
    check("lifecycle: the dropped loss does not fire", lost == 1 && engine.isConnected());
    check("lifecycle: third loss", loseDevice(engine));
    engine.close();
    internal::drainMainThreadQueue();
    check("lifecycle: close() before delivery drops the loss", lost == 1);

    // Rule 2 failure: false, and the failure is logged by init()
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("lifecycle: setup() returns false when the device cannot open",
          !engine.setup() && !engine.isConnected());
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);
    internal::drainMainThreadQueue();
    check("lifecycle: a failed setup() does not fire", lost == 1);

    // A stop notification while a device opened by name keeps running, as
    // Core Audio sends it when it moves such an output unit to the default
    // device: the delivered loss closes the device, nothing keeps playing.
    atomic<int> callbacks{0};
    EventListener counter = engine.audioOut.listen([&](AudioOutBuffer&) { ++callbacks; });
    check("lifecycle: reopen for the still-running loss", engine.setup(settings) && engine.isConnected());
    thread([] { internal::AudioDeviceLoss::stopped(); }).join();
    const int notified = callbacks.load();
    check("lifecycle: the stopped device still runs before delivery",
          !engine.isConnected() && waitFor([&] { return callbacks.load() > notified + 2; }));
    internal::drainMainThreadQueue();
    check("lifecycle: the still-running loss fires", lost == 2 && !engine.isConnected());
    const int released = callbacks.load();
    this_thread::sleep_for(chrono::milliseconds(100));
    check("lifecycle: no audio callback after the delivered loss", callbacks.load() == released);
    // Last: on a device still held, the seam itself would stop it
    check("lifecycle: the delivered loss holds no device", !internal::AudioDeviceLoss::injectStopForTests());
    check("lifecycle: setup(settings) brings the device back", engine.setup(settings) && engine.isConnected() &&
          waitFor([&] { return callbacks.load() > released; }));

    // An app that wants to go on playing on the default device reopens it
    // in the listener (setup() there; the Null backend here keeps the test
    // off real hardware). The lost device was released before it ran.
    AudioSettings defaults = settings;
    defaults.deviceName.clear();
    bool reopened = false;
    EventListener reopener = engine.onDisconnect.listen([&](DeviceDisconnectEventArgs&) {
        reopened = engine.setup(defaults);
    });
    check("lifecycle: loss for the reopening listener", loseDevice(engine));
    internal::drainMainThreadQueue();
    check("lifecycle: setup() inside onDisconnect reconnects", lost == 3 && reopened && engine.isConnected());
    reopener.disconnect();

    // Opened as the system default (as the listener above did): the device
    // follows the OS default and ignores "stopped" (WASAPI posts stopped,
    // rerouted, started on every unplug of the default device)
    thread([] { internal::AudioDeviceLoss::stopped(); }).join();
    check("lifecycle: a default-opened device stays connected on stopped", engine.isConnected());
    internal::drainMainThreadQueue();
    check("lifecycle: a default-opened device fires no onDisconnect", lost == 3 && engine.isConnected());
    const int kept = callbacks.load();
    check("lifecycle: a default-opened device keeps running after stopped",
          waitFor([&] { return callbacks.load() > kept + 2; }));
    check("lifecycle: a default-opened device is still held", internal::AudioDeviceLoss::injectStopForTests());
    // The engine's own release of a default device does not fire either
    engine.close();
    internal::drainMainThreadQueue();
    check("lifecycle: close() of a default-opened device does not fire", lost == 3 && !engine.isConnected());
    counter.disconnect();
    listener.disconnect();
    engine.shutdown();
}

} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    checkSelection();
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    checkBackendFailure();
    checkInit("implicit default", "");
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
    checkLifecycle();
    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
