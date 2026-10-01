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
//   - AudioEngine::waitForAudioCallbacks() returns true at once when no audio
//     runs: before init() and after shutdown().
//   - It waits for an audioOut pass that is already running: after
//     `disconnect(); waitForAudioCallbacks();` the listener is not running and
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
//   - The framework teardown waits for a stuck audioOut() without a time
//     limit: past one second the App is still not destroyed, one error is
//     logged, and the teardown goes on once audioOut() returns. Meanwhile the
//     public waitForAudioCallbacks() still gives up after about a second.
//   - An App runs once: Window::setApp() refuses an App whose window closed
//     (exit(), cleanup(), audio detached) with one error, keeps the window's
//     App, subscribes no hook and runs no second setup(); and it refuses any
//     App on a window that is not open (one error each, the window stays
//     empty), which leaves that App free to go to an open window.
//   - AudioRecorder::stop() waits for the audioOut pass in flight, and a
//     capture still in flight when stop() is called (held by a test hook
//     after its checks, before it hands the buffer to the writer) ends up in
//     the WAV and in getRecordedSeconds(): the writer keeps draining until
//     stop()'s barrier has passed.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

namespace {

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
// The hooks are subscribed right after setup() returns (#426, guarded by
// appAudioAttach), so audioOut() never runs during construction here; such a
// call would be counted apart (g_callsDuringCtor) and skipped.

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

// --- A stuck audioOut() at teardown ------------------------------------------
// Once armed, audioOut() blocks until the test releases it; update() asks to
// exit while it blocks. The framework teardown must keep waiting (no
// destruction under a running audioOut(), however long), log one error after
// a second, and go on once audioOut() returns.

static atomic<bool> g_stuckArmed{false}, g_stuckInside{false}, g_stuckRelease{false};
static atomic<bool> g_stuckDestroyed{false};
static bool g_stuckTimedOut = false;

struct StuckApp : App {
    int frames = 0;
    StuckApp() { g_stuckArmed = true; }
    ~StuckApp() override { g_stuckDestroyed = true; }
    void update() override {
        ++frames;
        if (g_stuckInside) requestExit();
        if (frames >= 2000) { g_stuckTimedOut = true; g_stuckRelease = true; requestExit(); }
    }
    void audioOut(AudioOutBuffer&) override {
        if (!g_stuckArmed.exchange(false)) return;
        g_stuckInside = true;
        while (!g_stuckRelease) this_thread::sleep_for(chrono::milliseconds(1));
        g_stuckInside = false;
    }
};

// --- A secondary window's App, and setApp()'s refusals -------------------------

struct WindowApp : App {
    atomic<int> setups{0};
    atomic<int> audioCalls{0};
    void setup() override { ++setups; }
    void audioOut(AudioOutBuffer&) override { ++audioCalls; }
};

// A Window made in a headless test has no native side: it is a closed window
// (isOpen() is false). OpenWindow stands in for an open one: its native_
// points at a dummy that nothing here dereferences (setApp() and tickTree()
// don't), and is cleared before ~Window() would close() it.
static int g_nativeStandIn = 0;
struct OpenWindow : Window {
    OpenWindow() { native_ = &g_nativeStandIn; }
    ~OpenWindow() { native_ = nullptr; }
};

// One tick of a secondary window, in its own context (as the platform glue
// does): setup() runs here on the window's first tick.
static void tickWindow(Window& w) {
    internal::WindowContext* prev = internal::currentWindowCtx();
    internal::currentWindowCtx() = &w.context();
    w.tickTree();
    internal::currentWindowCtx() = prev;
}

// -----------------------------------------------------------------------------

} // namespace

TC_CORE_TEST_MAIN() {
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
    vector<string> warnings, errors;
    EventListener logSub = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning && e.level != LogLevel::Error) return;
        lock_guard<mutex> lock(logMutex);
        (e.level == LogLevel::Warning ? warnings : errors).push_back(e.message);
    });
    auto countIn = [&](const vector<string>& list, const string& needle) {
        lock_guard<mutex> lock(logMutex);
        size_t n = 0;
        for (auto& w : list) if (w.find(needle) != string::npos) ++n;
        return n;
    };
    auto countWarnings = [&](const string& needle) { return countIn(warnings, needle); };
    auto countErrors = [&](const string& needle) { return countIn(errors, needle); };

    auto& engine = AudioEngine::getInstance();

    // --- no audio running -----------------------------------------------------
    {
        auto t0 = Clock::now();
        const bool ok = engine.waitForAudioCallbacks();
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
        const bool ok = engine.waitForAudioCallbacks();
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
        const bool ok = engine.waitForAudioCallbacks();
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
            result = engine.waitForAudioCallbacks();
            took = secondsSince(t0);
            done = true;
        });
        const bool ran = waitFor([&] { return done.load(); }, 3000);
        self.disconnect();
        engine.waitForAudioCallbacks();
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
        const size_t warnBefore = countWarnings("waitForAudioCallbacks");
        auto t0 = Clock::now();
        const bool ok = engine.waitForAudioCallbacks();
        const double took = secondsSince(t0);
        release = true;
        check("barrier gives up on a stuck listener after about a second",
              entered && !ok && took >= 0.9 && took < 3.0, ms(took));
        check("... and logs a warning", countWarnings("waitForAudioCallbacks") == warnBefore + 1);
        check("after the listener returns, the barrier returns true",
              engine.waitForAudioCallbacks() && !inside.load());
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
    check("audioOut() never ran during App construction (#426)", g_callsDuringCtor.load() == 0,
          to_string(g_callsDuringCtor.load()) + " calls");

    // --- teardown waits for a stuck audioOut(), without a time limit -------------------
    {
        const size_t errorsBefore = countErrors("has not returned");
        bool destroyedDuringHold = true;
        size_t errorsDuringHold = 0;
        atomic<bool> reached{false};
        bool publicResult = true;
        double publicTook = -1.0;
        thread releaser([&] {
            reached = waitFor([] { return g_stuckInside.load(); }, 5000);
            // The teardown is waiting by now (it holds the barrier's mutex).
            // The public barrier keeps its one-second limit meanwhile.
            this_thread::sleep_for(chrono::milliseconds(300));
            auto t1 = Clock::now();
            publicResult = engine.waitForAudioCallbacks();
            publicTook = secondsSince(t1);
            // Well past one second into the teardown's wait.
            this_thread::sleep_for(chrono::milliseconds(300));
            destroyedDuringHold = g_stuckDestroyed.load();
            errorsDuringHold = countErrors("has not returned");
            g_stuckRelease = true;
        });
        auto t0 = Clock::now();
        runHeadlessApp<StuckApp>();
        const double took = secondsSince(t0);
        releaser.join();
        check("teardown with a stuck audioOut() started", reached.load() && !g_stuckTimedOut);
        check("the App is not destroyed while its audioOut() is stuck, past one second",
              !destroyedDuringHold && took >= 1.5, ms(took));
        check("... one error says why the teardown waits",
              errorsDuringHold == errorsBefore + 1 &&
              countErrors("has not returned") == errorsBefore + 1,
              to_string(errorsDuringHold - errorsBefore) + " during the hold");
        check("... and the teardown goes on once audioOut() returns", g_stuckDestroyed.load());
        check("meanwhile the public barrier still gives up after about a second",
              !publicResult && publicTook >= 0.9 && publicTook < 1.5, ms(publicTook));
    }

    // --- setApp() refuses a cleaned-up App and a closed window ---------------------------
    // An App runs once. Window::close() needs a native window; here the test
    // runs its steps itself (exit(), cleanup(), internal::detachAppAudio(),
    // release, clear the native state) and then calls the real
    // Window::setApp().
    {
        auto mainApp = make_shared<App>();   // the main context's root, as runApp's App would be
        auto sub = make_shared<WindowApp>();
        auto keeper = make_shared<WindowApp>();
        auto stray = make_shared<WindowApp>();
        // None of the Apps above is subscribed yet: an App's hooks come right
        // after its first setup() (#426), on its window's first tick.
        const size_t hooks = engine.audioOut.listenerCount();
        OpenWindow first, second;
        Window closedWin;

        first.setApp(sub);
        first.applyPendingApp();   // the frame boundary
        tickWindow(first);
        const bool firstAudio = waitFor([&] { return sub->audioCalls.load() > 0; }, 2000);
        check("a new App attached to an open window: setup() once, audioOut() called, one hook",
              first.getApp() == sub && sub->setups.load() == 1 && firstAudio &&
              engine.audioOut.listenerCount() == hooks + 1,
              to_string(engine.audioOut.listenerCount()) + " hooks");

        // What the platform Window::close() does, its App part included.
        sub->exit();
        sub->cleanup();
        internal::detachAppAudio(*sub);
        first.setApp(nullptr);
        first.applyPendingApp();   // the frame boundary
        first.native_ = nullptr;
        const int callsAtClose = sub->audioCalls.load();
        this_thread::sleep_for(chrono::milliseconds(50));
        check("after the close, its audioOut() is no longer called",
              sub->audioCalls.load() == callsAtClose && engine.audioOut.listenerCount() == hooks);

        // The cleaned-up App on another open window, which already shows an App
        // (subscribed on this first tick).
        second.setApp(keeper);
        second.applyPendingApp();   // the frame boundary
        tickWindow(second);
        const size_t cleanupErrors = countErrors("already ran cleanup()");
        second.setApp(sub);
        second.applyPendingApp();   // the frame boundary
        tickWindow(second);
        this_thread::sleep_for(chrono::milliseconds(50));
        check("setApp() refuses an App whose cleanup() ran: one error",
              countErrors("already ran cleanup()") == cleanupErrors + 1);
        check("... the window keeps its App", second.getApp() == keeper && keeper->setups.load() == 1);
        check("... no hook comes back, audioOut() is not called",
              engine.audioOut.listenerCount() == hooks + 1 && sub->audioCalls.load() == callsAtClose,
              to_string(engine.audioOut.listenerCount()) + " hooks");
        check("... and its setup() does not run again", sub->setups.load() == 1);

        // A window that is not open: a closed one never runs close() again.
        const size_t closedErrors = countErrors("this window is closed");
        closedWin.setApp(stray);
        closedWin.applyPendingApp();   // the frame boundary
        check("setApp() on a window that is not open is refused: one error",
              countErrors("this window is closed") == closedErrors + 1);
        check("... the window stays empty", closedWin.getApp() == nullptr);
        first.setApp(stray);   // closed above
        first.applyPendingApp();   // the frame boundary
        check("... also one that was open before", first.getApp() == nullptr &&
              countErrors("this window is closed") == closedErrors + 2);
        second.setApp(nullptr);
        second.applyPendingApp();   // the frame boundary
        second.setApp(stray);   // the refusals left it free to attach
        second.applyPendingApp();   // the frame boundary
        check("the refused App can still go to an open window",
              second.getApp() == stray && engine.audioOut.listenerCount() == hooks + 1);
        second.setApp(nullptr);
        second.applyPendingApp();   // the frame boundary

        // keeper still has its hook (it was set up on second's tick): detach
        // and wait before it goes, so the audio thread (still running) cannot
        // be inside it meanwhile. stray and mainApp never ran setup(), so
        // they have none; detaching them is harmless.
        internal::detachAppAudio(*keeper);
        internal::detachAppAudio(*stray);
        internal::detachAppAudio(*mainApp);
    }

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
        engine.waitForAudioCallbacks();
        std::error_code ec;
        fs::remove(wav, ec);
    }

    // --- AudioRecorder::stop(): a capture in flight still reaches the file ---------------
    // A capture that passed its checks and copied its buffer, but has not handed
    // it to the writer yet, is held there (test hook) while another thread calls
    // stop(). The writer must keep draining until stop()'s barrier has seen that
    // capture finish: the buffer is in the WAV and in getRecordedSeconds().
    // Before, the writer followed running_, which stop() clears first; with
    // nothing pending it finished during the hold, and the buffer was lost.
    {
        static atomic<uint64_t> accepted{0};
        static atomic<bool> armed{false}, holding{false}, release{false};
        internal::setAudioRecorderCaptureHookForTests([](int frames) {
            accepted += (uint64_t)frames;
            if (!armed.exchange(false)) return;
            holding = true;
            while (!release) this_thread::sleep_for(chrono::milliseconds(1));
            holding = false;
        });
        const fs::path wav = fs::temp_directory_path() / "tc_audioListenerTeardown_inflight.wav";
        AudioRecorder rec;   // engine 2 ch -> stereo s16
        const bool recording = rec.start(wav);
        const bool some = waitFor([&] { return accepted.load() >= 2048; }, 3000);
        armed = true;
        const bool held = waitFor([&] { return holding.load(); }, 3000);
        thread stopper([&] { rec.stop(); });
        // Longer than the writer's 10 ms poll: a writer that followed running_
        // sees it cleared with nothing pending and finishes in here.
        this_thread::sleep_for(chrono::milliseconds(100));
        release = true;
        stopper.join();
        internal::setAudioRecorderCaptureHookForTests(nullptr);

        const uint64_t want = accepted.load();
        const uint64_t counted = (uint64_t)llround(rec.getRecordedSeconds() * settings.sampleRate);
        uint32_t dataBytes = 0;
        uintmax_t fileBytes = 0;
        {
            // S16 header with the JUNK chunk (#336): the data chunk is at byte 72.
            ifstream f(wav, ios::binary);
            char tag[4] = {};
            f.seekg(72);
            f.read(tag, 4);
            f.read(reinterpret_cast<char*>(&dataBytes), 4);
            if (string(tag, 4) != "data") dataBytes = 0;
            std::error_code ec;
            fileBytes = fs::file_size(wav, ec);
        }
        const uint64_t inFile = dataBytes / (2 * sizeof(int16_t));
        check("AudioRecorder records and a capture is held in flight", recording && some && held);
        check("the held capture's buffer is counted by getRecordedSeconds()", counted == want,
              to_string(counted) + " of " + to_string(want) + " frames");
        check("... and written to the WAV", inFile == want && fileBytes == 80u + dataBytes,
              to_string(inFile) + " of " + to_string(want) + " frames, " + to_string(fileBytes) + " bytes");
        std::error_code ec;
        fs::remove(wav, ec);
    }

    // --- after shutdown ---------------------------------------------------------------------
    engine.shutdown();
    {
        auto t0 = Clock::now();
        const bool ok = engine.waitForAudioCallbacks();
        const double took = secondsSince(t0);
        check("barrier after shutdown() returns true at once", ok && took < kAtOnce, ms(took));
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
