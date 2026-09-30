// =============================================================================
// core/tests/audioListenerTeardown — behavioral regression test for #256
// (items 2 and 3): once the framework, or AudioRecorder, lets an audioOut
// listener's owner go, nothing on the audio thread reaches it any more.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The real AudioEngine runs on miniaudio's null backend
// (internal::setNullAudioBackendForTests()), a device-less clock that still
// drives the mixer callback on its own thread, so no sound card is needed.
//
// Guards the invariants:
//   - AudioEngine::waitForCallbackIdle() returns true at once when no audio
//     runs: before init() and after shutdown().
//   - It waits for an audioOut pass that is already running: after
//     `disconnect(); waitForCallbackIdle();` the listener is not running and
//     is never called again (Event alone does not wait).
//   - Called from inside an audioOut listener (the audio thread) it returns
//     at once instead of waiting for itself.
//   - It gives up after about one second on a listener that is stuck, logs a
//     warning and returns false (the exit must not hang).
//   - App teardown, through runHeadlessApp (the windowed exit, hot reload and
//     closing a secondary window call the same internal::detachAppAudio()),
//     started while the App's audioOut() is in flight: cleanup() still has the
//     hook attached, the hook is gone afterwards, the App's destructor never
//     starts while audioOut() runs, and audioOut() never runs once the
//     destructor has started. Before #256 the audio thread kept calling the
//     derived audioOut() until ~App() disconnected it, after the derived
//     members were already gone.
//   - AudioRecorder::stop() waits for the audioOut pass in flight.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-72s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

using Clock = chrono::steady_clock;
static double secondsSince(Clock::time_point t0) {
    return chrono::duration<double>(Clock::now() - t0).count();
}

// Wait (up to `timeoutMs`) until `pred()` holds.
template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 1) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return pred();
}

static string ms(double s) { return to_string((int)(s * 1000.0)) + " ms"; }

// "At once": no callback to wait for. Generous for a loaded CI machine; a
// wait for a real callback in these tests is 200 ms or more.
static constexpr double kAtOnce = 0.1;

// --- The App shape ------------------------------------------------------------
// audioOut() reads a member of the derived App. After a few ordinary calls one
// call runs long, and update() asks to exit while it runs, so the teardown
// (exit(), cleanup(), detach, destruction) meets an audioOut() in flight every
// time. The derived destructor marks the App as going first, then lingers so
// that an audio thread still calling audioOut() is caught doing it.
//
// App() subscribes the hooks in its own constructor, so the audio thread can
// reach audioOut() before the SynthApp constructor has finished. That
// construction-side window is outside #256: such calls are counted apart
// (g_callsDuringCtor) and skipped.

enum AppState { Constructing, Alive, Destructing };
static atomic<int>  g_state{Constructing};
static atomic<int>  g_inAudioOut{0};
static atomic<int>  g_audioOutCalls{0};     // while the App was fully constructed
static atomic<bool> g_longCallRunning{false};
static atomic<int>  g_callsDuringCtor{0};
static atomic<int>  g_callsAfterDtor{0};    // audioOut() ran once ~SynthApp() had started
static atomic<int>  g_dtorDuringCall{0};    // ~SynthApp() started while audioOut() ran
static int  g_cleanupCalls = 0;
static size_t g_hooksAtCleanup = 0;         // audioOut listeners while cleanup() runs
static bool g_timedOut = false;

struct SynthApp : App {
    vector<float> wavetable = vector<float>(4096, 0.5f);
    int frames = 0;

    SynthApp() { g_state = Alive; }
    ~SynthApp() override {
        if (g_inAudioOut.load() != 0) ++g_dtorDuringCall;
        g_state = Destructing;
        // Before #256 the audio thread kept calling audioOut() from here on.
        // Longer than the long call, so its end falls in here too.
        this_thread::sleep_for(chrono::milliseconds(150));
    }

    void update() override {
        ++frames;
        if (g_longCallRunning) requestExit();   // tear down during the long call
        if (frames >= 2000) { g_timedOut = true; requestExit(); }
    }
    // The hooks are detached AFTER cleanup(): it can still use audio.
    void cleanup() override {
        ++g_cleanupCalls;
        g_hooksAtCleanup = AudioEngine::getInstance().audioOut.listenerCount();
    }

    void audioOut(AudioOutBuffer& b) override {
        ++g_inAudioOut;
        const int state = g_state.load();
        if (state == Constructing) { ++g_callsDuringCtor; --g_inAudioOut; return; }
        if (state == Destructing) { ++g_callsAfterDtor; --g_inAudioOut; return; }
        // The 5th call runs long (the exit starts meanwhile), the others 2 ms.
        const bool longCall = ++g_audioOutCalls == 5;
        if (longCall) g_longCallRunning = true;
        this_thread::sleep_for(chrono::milliseconds(longCall ? 100 : 2));
        if (longCall) g_longCallRunning = false;
        if (g_state.load() == Destructing) { ++g_callsAfterDtor; --g_inAudioOut; return; }
        for (int i = 0; i < b.frameCount * b.channels; ++i) {
            b.data[i] += 0.0f * wavetable[(size_t)(i * 7) & 4095];
        }
        --g_inAudioOut;
    }
};

// -----------------------------------------------------------------------------

int main() {
    // A barrier that never returns would hang CI; fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout (a barrier or a teardown did not return)\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    // Device-less engine; set before anything opens a context.
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();   // this thread is the main thread

    mutex logMutex;
    vector<string> warnings;
    EventListener logSub = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning) return;
        lock_guard<mutex> lock(logMutex);
        warnings.push_back(e.message);
    });
    auto countWarnings = [&](const string& needle) {
        lock_guard<mutex> lock(logMutex);
        size_t n = 0;
        for (auto& w : warnings) if (w.find(needle) != string::npos) ++n;
        return n;
    };

    auto& engine = AudioEngine::getInstance();

    // --- no audio running -----------------------------------------------------
    {
        auto t0 = Clock::now();
        const bool ok = engine.waitForCallbackIdle();
        const double took = secondsSince(t0);
        check("barrier before init() returns true at once", ok && took < kAtOnce, ms(took));
    }

    AudioSettings settings;
    settings.sampleRate = 48000;
    settings.channels = 2;
    settings.bufferSize = 256;
    const bool started = engine.init(settings);
    check("engine starts on the null backend", started && engine.isInitialized());
    if (!started) return 1;

    // --- the barrier waits for a pass in flight --------------------------------
    {
        atomic<bool> inside{false};
        atomic<int> calls{0};
        EventListener slow = engine.audioOut.listen([&](AudioOutBuffer&) {
            inside = true;
            ++calls;
            this_thread::sleep_for(chrono::milliseconds(200));
            inside = false;
        });
        const bool entered = waitFor([&] { return inside.load(); }, 2000);
        check("a slow audioOut listener runs on the audio thread", entered);
        slow.disconnect();
        auto t0 = Clock::now();
        const bool ok = engine.waitForCallbackIdle();
        const double took = secondsSince(t0);
        check("barrier waits for the listener already running",
              ok && !inside.load() && took >= 0.05, ms(took));
        const int callsAfter = calls.load();
        this_thread::sleep_for(chrono::milliseconds(50));
        check("the disconnected listener is not called again", calls.load() == callsAfter);
    }

    // --- engine running, nothing slow in flight -----------------------------------
    {
        auto t0 = Clock::now();
        const bool ok = engine.waitForCallbackIdle();
        const double took = secondsSince(t0);
        check("barrier with only fast callbacks returns true quickly", ok && took < kAtOnce, ms(took));
    }

    // --- called from the audio thread ------------------------------------------------
    {
        atomic<bool> done{false};
        atomic<bool> result{false};
        atomic<double> took{-1.0};
        EventListener self = engine.audioOut.listen([&](AudioOutBuffer&) {
            if (done) return;
            auto t0 = Clock::now();
            result = engine.waitForCallbackIdle();
            took = secondsSince(t0);
            done = true;
        });
        const bool ran = waitFor([&] { return done.load(); }, 3000);
        self.disconnect();
        engine.waitForCallbackIdle();
        check("barrier called inside an audioOut listener returns at once",
              ran && result.load() && took.load() >= 0.0 && took.load() < kAtOnce,
              ran ? ms(took.load()) : "the listener never finished");
    }

    // --- a stuck listener: bounded wait ------------------------------------------------
    {
        atomic<bool> release{false};
        atomic<bool> inside{false};
        EventListener stuck = engine.audioOut.listen([&](AudioOutBuffer&) {
            inside = true;
            while (!release) this_thread::sleep_for(chrono::milliseconds(1));
            inside = false;
        });
        const bool entered = waitFor([&] { return inside.load(); }, 2000);
        stuck.disconnect();
        const size_t warnBefore = countWarnings("waitForCallbackIdle");
        auto t0 = Clock::now();
        const bool ok = engine.waitForCallbackIdle();
        const double took = secondsSince(t0);
        release = true;
        check("barrier gives up on a stuck listener after about a second",
              entered && !ok && took >= 0.9 && took < 3.0, ms(took));
        check("... and logs a warning", countWarnings("waitForCallbackIdle") == warnBefore + 1);
        check("after the listener returns, the barrier returns true",
              engine.waitForCallbackIdle() && !inside.load());
    }

    // --- App teardown ---------------------------------------------------------------------
    // Three App lifetimes: each exit must detach audioOut() before the App goes.
    for (int run = 0; run < 3; ++run) {
        g_audioOutCalls = 0;
        g_longCallRunning = false;
        g_state = Constructing;
        runHeadlessApp<SynthApp>();
        const string tag = "run " + to_string(run + 1) + ": ";
        check(tag + "the exit ran while a long audioOut() was in flight",
              g_audioOutCalls.load() >= 5 && !g_timedOut,
              to_string(g_audioOutCalls.load()) + " calls");
        check(tag + "cleanup() ran with the App's audioOut() still attached",
              g_cleanupCalls == run + 1 && g_hooksAtCleanup == 1,
              to_string(g_hooksAtCleanup) + " listeners");
        check(tag + "the App's hook is gone after the exit", engine.audioOut.listenerCount() == 0);
        check(tag + "audioOut() never ran after the destructor started",
              g_callsAfterDtor.load() == 0, to_string(g_callsAfterDtor.load()) + " calls");
        check(tag + "the destructor never started while audioOut() ran",
              g_dtorDuringCall.load() == 0);
    }
    printf("(info) audioOut() calls during App construction, outside #256: %d\n",
           g_callsDuringCtor.load());

    // --- AudioRecorder::stop() waits for the pass in flight ------------------------------
    {
        const fs::path wav = fs::temp_directory_path() / "tc_audioListenerTeardown.wav";
        AudioRecorder rec;
        const bool recording = rec.start(wav);
        check("AudioRecorder starts", recording);
        atomic<bool> inside{false};
        atomic<bool> slept{false};
        // After the recorder's Monitor priority: the same pass, later. Slow
        // once only, so the pass after the one stop() waits for is not.
        EventListener slow = engine.audioOut.listen([&](AudioOutBuffer&) {
            if (slept.exchange(true)) return;
            inside = true;
            this_thread::sleep_for(chrono::milliseconds(300));
            inside = false;
        }, audio::priority::Monitor + 50);
        const bool entered = waitFor([&] { return inside.load(); }, 2000);
        auto t0 = Clock::now();
        rec.stop();
        const double took = secondsSince(t0);
        check("AudioRecorder::stop() returns after the audioOut pass in flight",
              entered && !inside.load(), ms(took));
        slow.disconnect();
        engine.waitForCallbackIdle();
        std::error_code ec;
        fs::remove(wav, ec);
    }

    // --- after shutdown ---------------------------------------------------------------------
    engine.shutdown();
    {
        auto t0 = Clock::now();
        const bool ok = engine.waitForCallbackIdle();
        const double took = secondsSince(t0);
        check("barrier after shutdown() returns true at once", ok && took < kAtOnce, ms(took));
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
