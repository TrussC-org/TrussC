#include <TrussC.h>
#include "tc/sound/tcMicInputInternal.h"
#include "../../common/tcCoreTest.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace tc;
namespace {
#ifndef __EMSCRIPTEN__
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
template<class Predicate> void waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) std::_Exit(2);
        std::this_thread::yield();
    }
}
struct InputApp : App {
    int calls = 0;
    uint64_t frames = 0;
    bool ordered = true;
    std::atomic<bool> hold{false}, entered{false}, release{false}, finished{false};
    void audioIn(const AudioInBuffer& b) override {
        ordered &= b.framePosition == frames && !isMainThread();
        frames += b.frameCount;
        ++calls;
        if (hold) {
            entered = true;
            waitFor([&] { return release.load(); });
            finished = true;
        }
    }
};

template<class Stop> void checkDrain(const char* label,
                                    const std::shared_ptr<internal::MicState>& state,
                                    Stop stop) {
    auto& engine = AudioEngine::getInstance();
    std::atomic<bool> entered{false}, release{false}, finished{false};
    bool draining = false;
    int calls = 0;
    auto listener = engine.audioIn.listen([&](AudioInBuffer&) {
        ++calls;
        entered = true;
        waitFor([&] { return release.load(); });
        finished = true;
    });
    // Rendezvous with the existing barrier's diagnostic, rather than racing
    // stop's return or sleeping to guess whether it has begun waiting.
    auto log = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error &&
            e.message.find("teardown keeps waiting") != std::string::npos) {
            draining = true;
            release = true;
        }
    });
    const float sample = 0;
    std::thread capture([&] { state->capture(&sample, 1); });
    waitFor([&] { return entered.load(); });
    stop();
    const bool finishedOnReturn = finished.load();
    release = true; // also let a broken, early-returning implementation finish
    capture.join();
    state->capture(&sample, 1);
    check(label, draining && finishedOnReturn && calls == 1);
}
#endif
}

TC_CORE_TEST_MAIN() {
#ifdef __EMSCRIPTEN__
    std::puts("SKIP: native capture delivery; Web behavior is unchanged");
    return 0;
#else
    getMainThreadId();
    auto& engine = AudioEngine::getInstance();
    std::shared_ptr<internal::MicState> state;
    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::Authorized; }, {});
    internal::setMicBackendForTests([&](internal::MicState& opened) {
        state = opened.shared_from_this();
        opened.name = "fake capture";
        return MicStartResult{true, {}};
    }, [](internal::MicState&) {});
    MicInput mic;
    auto app = internal::makeConstructedApp<InputApp>();
    auto otherApp = internal::makeConstructedApp<InputApp>();
    internal::setupNodeOnce(*app);
    internal::setupNodeOnce(*otherApp);
    check("subscribing Apps does not start capture or playback", !mic.isRunning() && !engine.isInitialized());
    check("explicit MicInput start opens the fake device", mic.start(32000) && state);
    if (!state) return 1;

    const std::array<int, 6> sizes{1, 7, 256, MicInput::BUFFER_SIZE + 3, 3, 512};
    std::array<float, MicInput::BUFFER_SIZE + 3> samples{};
    uint64_t frames = 0;
    int calls = 0, nextListener = 0;
    bool valid = true;
    auto first = engine.audioIn.listen([&](AudioInBuffer& b) {
        valid &= nextListener == 0 && b.data == samples.data() && b.channels == 1 &&
                 b.sampleRate == 32000 && b.framePosition == frames &&
                 b.frameCount == sizes[calls] && b.data[0] == float(calls + 1) &&
                 !isMainThread() && internal::isInsideAudioCallback();
        // Ring access from a listener must work: notification holds no ring lock.
        float latest = 0;
        valid &= mic.getBuffer(&latest, 1) == 1 && latest == b.data[b.frameCount - 1];
        valid &= engine.waitForAudioCallbacks(); // the existing self-wait bypass
        nextListener = 1;
    });
    auto second = engine.audioIn.listen([&](AudioInBuffer& b) {
        valid &= nextListener == 1 && !isMainThread();
        nextListener = 0;
        frames += b.frameCount;
        ++calls;
    }, Deliver::Inline);
    std::thread capture([&] {
        for (int count : sizes) {
            samples.fill(float(calls + 1));
            state->capture(samples.data(), count);
        }
    });
    capture.join();
    check("every block is delivered inline, in order, including explicit Inline", valid && calls == int(sizes.size()));
    check("every App receives every captured block in order", app->ordered && otherApp->ordered &&
          app->calls == calls && otherApp->calls == calls && app->frames == frames && otherApp->frames == frames);
    first.disconnect(); second.disconnect();
    internal::detachAppAudio(*otherApp);

    app->hold = true;
    std::thread blockedCapture([&] { state->capture(samples.data(), 1); });
    waitFor([&] { return app->entered.load(); });
    std::thread release([&] {
        waitFor([&] { return engine.audioIn.listenerCount() == 0; });
        app->release = true;
    });
    internal::detachAppAudio(*app); // the same #256 path used before App cleanup
    check("App detachment waits for the in-flight capture callback", app->finished);
    release.join(); blockedCapture.join();
    const int detachedCalls = app->calls;
    std::thread laterCapture([&] { state->capture(samples.data(), 1); });
    laterCapture.join();
    check("capture cannot reach detached Apps", app->calls == detachedCalls && otherApp->calls == calls);
    check("capture-only delivery leaves playback uninitialized", !engine.isInitialized());
    checkDrain("stop drains a held audioIn and closes admission", state, [&] { mic.stop(); });

    auto scopedMic = std::make_unique<MicInput>();
    check("scoped MicInput starts", scopedMic->start());
    checkDrain("destructor drains a held audioIn and closes admission", state, [&] { scopedMic.reset(); });

    check("MicInput restarts before engine shutdown", mic.start());
    checkDrain("shutdown drains a held audioIn and closes admission", state, [&] { engine.shutdown(); });
    check("shutdown leaves MicInput running", mic.isRunning());
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    check("init reopens input admission", engine.init(settings));
    int resumedCalls = 0;
    auto resumed = engine.audioIn.listen([&](AudioInBuffer&) { ++resumedCalls; });
    state->capture(samples.data(), 1);
    check("running MicInput notifies again after init", resumedCalls == 1);
    mic.stop();
    engine.shutdown();
    internal::waitForResourceLoadsForTests();
    internal::setMicBackendForTests({}, {});
    internal::setMicAuthorizationForTests({}, {});
    return failures ? 1 : 0;
#endif
}
