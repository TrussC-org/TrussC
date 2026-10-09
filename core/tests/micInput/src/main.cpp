#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace std;
using namespace tc;

namespace {
#ifndef __EMSCRIPTEN__
int failures = 0;

void check(const char* name, bool ok) {
    printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

template<class Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while (!predicate()) {
        if (chrono::steady_clock::now() >= deadline) return false;
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return true;
}

bool captureAdvances(MicInput& mic) {
    // The Null capture callback writes silence. Seed the latest sample with a
    // marker and wait for a real callback to replace it, even after re-init.
    const float marker = 1.0f;
    mic.onAudioData(&marker, 1);
    return waitFor([&] {
        float sample = marker;
        return mic.getBuffer(&sample, 1) == 1 && sample == 0.0f;
    });
}
#endif
}

TC_CORE_TEST_MAIN() {
#ifdef __EMSCRIPTEN__
    printf("SKIP: native miniaudio capture; Web uses browser capture\n");
    return 0;
#else
    getMainThreadId();
    auto& engine = AudioEngine::getInstance();
    MicInput mic;

    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    settings.bufferSize = 256;

    // Select a deterministic capture backend without opening playback. Keep
    // playback unavailable: MicInput must not attempt lazy engine init.
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("playback open failure leaves engine stopped", !engine.init(settings) && !engine.isInitialized());
    check("mic starts without playback", mic.start());
    check("mic-only start does not initialize playback", !engine.isInitialized());
    check("mic-only capture callbacks advance", captureAdvances(mic));
    mic.stop();
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);

    check("Null engine starts", engine.init(settings));
    if (!engine.isInitialized()) return 1;

    Sound sound;
    sound.loadTestTone(440, 60);
    sound.setLoop(true);
    check("Sound starts", sound.play());
    check("Sound advances before capture", waitFor([&] { return sound.getPosition() > 0; }));
    for (int cycle = 0; cycle < 3; ++cycle) {
        check("mic starts with a private Null context", mic.start());
        const float duringCapture = sound.getPosition();
        check("Sound advances during capture", waitFor([&] {
            return sound.getPosition() > duringCapture;
        }));
        mic.stop();
        const float afterStop = sound.getPosition();
        check("mic stop clears its state", !mic.isRunning() && mic.getDeviceName().empty());
        check("Sound advances after mic stop", waitFor([&] {
            return sound.getPosition() > afterStop;
        }));
        check("engine remains running", engine.isInitialized() && sound.isPlaying());
    }

    check("mic starts before sample-rate re-init", mic.start());
    settings.sampleRate = 44100;
    check("sample-rate re-init retains capture", engine.init(settings) && mic.isRunning());
    check("capture callbacks advance after sample-rate re-init", captureAdvances(mic));
    const float afterReinit = sound.getPosition();
    check("Sound advances after sample-rate re-init", waitFor([&] {
        return sound.getPosition() > afterReinit;
    }));
    sound.stop();
    engine.shutdown();
    check("engine shutdown leaves private capture running", !engine.isInitialized() && mic.isRunning());
    check("capture callbacks advance after engine shutdown", captureAdvances(mic));
    check("engine can restart alongside capture", engine.init(settings));
    check("capture callbacks advance after engine restart", captureAdvances(mic));

    // Replace the engine context while the mic is using its private Null
    // context. No physical output is opened, even on machines with hardware.
    settings.backend = AudioBackend::Default;
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::OpenFails);
    check("backend replacement leaves playback stopped", !engine.init(settings) && !engine.isInitialized());
    check("backend replacement leaves private capture running", mic.isRunning());
    check("capture callbacks advance after backend replacement", captureAdvances(mic));
    internal::setAudioDeviceFaultForTests(internal::AudioDeviceFaultForTests::None);
    settings.backend = AudioBackend::Null;
    check("Null engine recovers after backend replacement", engine.init(settings));
    mic.stop();
    check("mic can restart after backend replacement", mic.start());
    mic.stop();
    {
        MicInput scopedMic;
        check("scoped mic starts", scopedMic.start());
    }
    engine.shutdown();
    return failures ? 1 : 0;
#endif
}
