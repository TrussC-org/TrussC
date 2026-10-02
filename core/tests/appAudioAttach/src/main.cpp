// =============================================================================
// core/tests/appAudioAttach — behavioral regression test for #426: an App's
// audioOut() / audioIn() are subscribed right after its first setup() has
// returned, never earlier, and only once.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The real AudioEngine runs on miniaudio's null backend
// (internal::setNullAudioBackendForTests()), a device-less clock that still
// drives the mixer callback on its own thread, so no sound card is needed.
//
// Before #426 the App constructor subscribed the hooks: with audio running,
// audioOut() ran before setup() (and while it ran), so state that setup()
// prepares was not there yet, and an App that was never run got callbacks.
//
// Guards the invariants:
//   - The main App (runHeadlessApp): the App's setup() allocates what its
//     audioOut() reads; no audioOut() runs before setup() has returned, no
//     hook is subscribed while setup() runs, and afterwards exactly one
//     audioOut and one audioIn hook are, until the exit detaches them.
//   - A secondary window's App (Window::setApp(), setup() on the window's
//     first tick): nothing is subscribed at setApp(), the hooks come right
//     after that setup(), and further ticks, or a second window the App moves
//     to, add no second hook and run no second setup().
//   - An App that is constructed but never run (make_shared or on the stack)
//     is never subscribed and gets no callbacks.
//   - The App's hooks still run ahead of the default-priority audioOut
//     listeners its setup() subscribed, as when the constructor subscribed
//     them: a tap there sees what audioOut() wrote.
//   - The attach is idempotent (a second attach adds no hook) and never
//     happens again once the App's lifecycle has ended
//     (internal::detachAppAudio(): an App runs once).
// The hot reload generation's path is covered in hotReloadLifecycle.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

// Wait (up to `timeoutMs`) until `pred()` holds.
template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 1) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return pred();
}

// --- The App shape ------------------------------------------------------------
// setup() allocates the table audioOut() reads, the usual pattern. It takes a
// while (the sleep), so a hook subscribed before or during setup() is called
// several times in it: one audio pass is 256 frames, about 5 ms at 48 kHz.
// What the App sees goes to a Probe that outlives it.

struct Probe {
    atomic<int>  setups{0};
    atomic<bool> setupReturned{false};
    atomic<int>  callsBeforeSetup{0};    // audioOut() before setup() returned
    atomic<int>  callsWithoutTable{0};   // audioOut() found setup()'s table missing
    atomic<int>  audioOutCalls{0};       // ordinary calls, after setup()
    atomic<long> outHooksInSetup{-1};    // audioOut listeners while setup() runs
    atomic<long> inHooksInSetup{-1};
    atomic<long> outHooksInUpdate{-1};   // ... and in the last update()
    atomic<long> inHooksInUpdate{-1};
    atomic<bool> tagBuffer{false};       // audioOut() adds kTag to data[0]
    bool timedOut = false;
};

static Probe* g_nextProbe = nullptr;   // the Probe the next App reports to

static constexpr size_t kTableSize = 1024;
static constexpr float kTag = 1000.0f;   // far above anything mixed here

struct SetupAllocApp : App {
    // No user-provided constructor: make_shared value-initialises, so these
    // read as null / empty before the members are initialised.
    Probe* probe = g_nextProbe;
    vector<float> table;
    int frames = 0;
    bool exitAfterAudio = false;   // runHeadlessApp: exit once audio has run

    void setup() override {
        Probe& p = *probe;
        ++p.setups;
        auto& engine = AudioEngine::getInstance();
        p.outHooksInSetup = (long)engine.audioOut.listenerCount();
        p.inHooksInSetup = (long)engine.audioIn.listenerCount();
        this_thread::sleep_for(chrono::milliseconds(60));
        table.assign(kTableSize, 0.25f);
        p.setupReturned = true;
    }

    void update() override {
        ++frames;
        if (!exitAfterAudio) return;
        auto& engine = AudioEngine::getInstance();
        probe->outHooksInUpdate = (long)engine.audioOut.listenerCount();
        probe->inHooksInUpdate = (long)engine.audioIn.listenerCount();
        if (probe->audioOutCalls.load() >= 20) requestExit();
        if (frames >= 600) { probe->timedOut = true; requestExit(); }
    }

    void audioOut(AudioOutBuffer& b) override {
        Probe* p = probe;
        if (!p) return;   // only an audioOut() during construction sees this
        if (!p->setupReturned.load()) { ++p->callsBeforeSetup; return; }
        if (table.size() != kTableSize) { ++p->callsWithoutTable; return; }
        for (int i = 0; i < b.frameCount * b.channels; ++i) {
            b.data[i] += 0.0f * table[(size_t)i % kTableSize];
        }
        if (p->tagBuffer.load()) b.data[0] += kTag;
        ++p->audioOutCalls;
    }
};

// setup() subscribes a default-priority audioOut listener, the pattern the
// App audio comment recommends next to the override. It counts the passes in
// which the App's audioOut() (which tags data[0]) had already run.
struct TapInSetupApp : SetupAllocApp {
    atomic<int> appFirst{0}, appLater{0};
    EventListener tap;

    void setup() override {
        tap = AudioEngine::getInstance().audioOut.listen([this](AudioOutBuffer& b) {
            if (b.data[0] > kTag * 0.5f) { ++appFirst; b.data[0] -= kTag; }
            else ++appLater;
        });
        SetupAllocApp::setup();
    }
};

struct HeadlessApp : SetupAllocApp {
    HeadlessApp() { exitAfterAudio = true; }
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

static string hooks(size_t n) { return to_string(n) + " hooks"; }

// -----------------------------------------------------------------------------

} // namespace

TC_CORE_TEST_MAIN() {
    // A wait that never ends would hang CI; fail loudly instead.
    thread([] {
        this_thread::sleep_for(chrono::seconds(60));
        printf("FAIL: watchdog timeout\n");
        fflush(stdout);
        _Exit(3);
    }).detach();

    // Device-less engine; set before anything opens a context.
    internal::setNullAudioBackendForTests(true);
    getMainThreadId();   // this thread is the main thread

    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = 48000;
    settings.channels = 2;
    settings.bufferSize = 256;
    const bool started = engine.init(settings);
    check("engine starts on the null backend", started && engine.isInitialized());
    if (!started) return 1;

    // Audio is running from here on; no other listener is attached.
    const size_t outBase = engine.audioOut.listenerCount();
    const size_t inBase = engine.audioIn.listenerCount();

    // --- the main App: runHeadlessApp ---------------------------------------------
    {
        Probe p;
        g_nextProbe = &p;
        runHeadlessApp<HeadlessApp>();
        check("main App: setup() ran once, then audioOut() was called",
              p.setups.load() == 1 && p.audioOutCalls.load() >= 20 && !p.timedOut,
              to_string(p.audioOutCalls.load()) + " calls");
        check("main App: no audioOut() before setup() returned",
              p.callsBeforeSetup.load() == 0, to_string(p.callsBeforeSetup.load()) + " calls");
        check("main App: audioOut() always found what setup() allocated",
              p.callsWithoutTable.load() == 0, to_string(p.callsWithoutTable.load()) + " calls");
        check("main App: no hook subscribed while setup() ran",
              p.outHooksInSetup.load() == (long)outBase && p.inHooksInSetup.load() == (long)inBase,
              to_string(p.outHooksInSetup.load()) + " audioOut / " +
              to_string(p.inHooksInSetup.load()) + " audioIn listeners");
        check("main App: exactly one audioOut and one audioIn hook while it ran",
              p.outHooksInUpdate.load() == (long)outBase + 1 &&
              p.inHooksInUpdate.load() == (long)inBase + 1,
              to_string(p.outHooksInUpdate.load()) + " audioOut / " +
              to_string(p.inHooksInUpdate.load()) + " audioIn listeners");
        check("main App: the hooks are gone after the exit",
              engine.audioOut.listenerCount() == outBase && engine.audioIn.listenerCount() == inBase);
    }

    // --- an App that is never run ---------------------------------------------------
    {
        Probe p;
        g_nextProbe = &p;
        {
            auto kept = make_shared<SetupAllocApp>();   // e.g. kept for a later setApp()
            SetupAllocApp onStack;
            check("an App that is constructed is not subscribed",
                  engine.audioOut.listenerCount() == outBase && engine.audioIn.listenerCount() == inBase,
                  hooks(engine.audioOut.listenerCount()));
            this_thread::sleep_for(chrono::milliseconds(100));   // ~20 audio passes
        }
        check("an App that is never run gets no audioOut() calls",
              p.callsBeforeSetup.load() == 0 && p.audioOutCalls.load() == 0 && p.setups.load() == 0,
              to_string(p.callsBeforeSetup.load()) + " calls");
    }

    // --- a secondary window's App: setup() on the window's first tick ------------------
    {
        Probe p;
        g_nextProbe = &p;
        auto sub = make_shared<SetupAllocApp>();
        OpenWindow win, other;
        win.setApp(sub);
        this_thread::sleep_for(chrono::milliseconds(50));
        check("window App: setApp() alone subscribes nothing",
              engine.audioOut.listenerCount() == outBase && p.callsBeforeSetup.load() == 0,
              hooks(engine.audioOut.listenerCount()));

        tickWindow(win);   // setup(), then the hooks
        const bool called = waitFor([&] { return p.audioOutCalls.load() >= 5; }, 2000);
        check("window App: its first tick runs setup(), then audioOut() is called",
              p.setups.load() == 1 && called, to_string(p.audioOutCalls.load()) + " calls");
        check("window App: no audioOut() before setup() returned",
              p.callsBeforeSetup.load() == 0 && p.callsWithoutTable.load() == 0,
              to_string(p.callsBeforeSetup.load()) + " before setup, " +
              to_string(p.callsWithoutTable.load()) + " without the table");
        check("window App: no hook subscribed while setup() ran",
              p.outHooksInSetup.load() == (long)outBase && p.inHooksInSetup.load() == (long)inBase);
        check("window App: one audioOut and one audioIn hook after setup()",
              engine.audioOut.listenerCount() == outBase + 1 &&
              engine.audioIn.listenerCount() == inBase + 1,
              hooks(engine.audioOut.listenerCount()));

        for (int i = 0; i < 3; ++i) tickWindow(win);
        check("window App: later ticks add no hook and run no second setup()",
              engine.audioOut.listenerCount() == outBase + 1 &&
              engine.audioIn.listenerCount() == inBase + 1 && p.setups.load() == 1,
              hooks(engine.audioOut.listenerCount()));

        // Moved to another window without closing (no cleanup()): it is
        // already set up, so it stays subscribed once.
        win.setApp(nullptr);
        other.setApp(sub);
        tickWindow(other);
        check("window App moved to another window: no second hook, no second setup()",
              other.getApp() == sub && engine.audioOut.listenerCount() == outBase + 1 &&
              engine.audioIn.listenerCount() == inBase + 1 && p.setups.load() == 1,
              hooks(engine.audioOut.listenerCount()));

        // What the platform Window::close() does, its App part included.
        sub->exit();
        sub->cleanup();
        internal::detachAppAudio(*sub);
        other.setApp(nullptr);
        other.native_ = nullptr;
        check("window App: closing its window removes both hooks",
              engine.audioOut.listenerCount() == outBase && engine.audioIn.listenerCount() == inBase);
    }

    // --- order: the App's hooks before default listeners setup() subscribed -----------
    {
        Probe p;
        g_nextProbe = &p;
        p.tagBuffer = true;
        auto app = make_shared<TapInSetupApp>();
        // Subscribed before the App's hooks too, at the default priority.
        atomic<int> earlyFirst{0}, earlyLater{0};
        EventListener early = engine.audioOut.listen([&](AudioOutBuffer& b) {
            if (b.data[0] > kTag * 0.5f) ++earlyFirst; else ++earlyLater;
        });
        internal::setupNodeOnce(*app);
        engine.waitForAudioCallbacks();
        app->appFirst = 0;
        app->appLater = 0;
        earlyFirst = 0;
        earlyLater = 0;
        const bool passes = waitFor([&] {
            return app->appFirst.load() + app->appLater.load() >= 10;
        }, 2000);
        early.disconnect();
        app->tap.disconnect();
        engine.waitForAudioCallbacks();
        check("App's audioOut() runs before a default listener setup() subscribed",
              passes && app->appLater.load() == 0,
              to_string(app->appFirst.load()) + " passes with the App first, " +
              to_string(app->appLater.load()) + " with it later");
        check("... and before one subscribed before the App was set up",
              earlyFirst.load() > 0 && earlyLater.load() == 0,
              to_string(earlyFirst.load()) + " / " + to_string(earlyLater.load()));
        internal::detachAppAudio(*app);
        check("order: the hooks are gone after the detach",
              engine.audioOut.listenerCount() == outBase && engine.audioIn.listenerCount() == inBase,
              hooks(engine.audioOut.listenerCount()));
    }

    // --- the attach itself: once, and never after the App's end -----------------------
    {
        Probe p;
        g_nextProbe = &p;
        auto app = make_shared<SetupAllocApp>();
        internal::setupNodeOnce(*app);
        internal::setupNodeOnce(*app);
        check("setup once: setup() ran once, one hook each",
              p.setups.load() == 1 && engine.audioOut.listenerCount() == outBase + 1 &&
              engine.audioIn.listenerCount() == inBase + 1,
              hooks(engine.audioOut.listenerCount()));
        // A listener at the App hooks' own priority, subscribed after them,
        // runs after them in every pass. A second attach must keep the App's
        // subscription as it is: re-subscribing it would move it behind this
        // one (and could skip a pass meanwhile).
        atomic<int> appFirst{0}, appLater{0};
        EventListener after = engine.audioOut.listen([&](AudioOutBuffer& b) {
            if (b.data[0] > kTag * 0.5f) { ++appFirst; b.data[0] -= kTag; }
            else ++appLater;
        }, internal::appAudioPriority);
        p.tagBuffer = true;
        internal::attachAppAudio(*app);
        engine.waitForAudioCallbacks();
        appFirst = 0;
        appLater = 0;
        const bool passes = waitFor([&] { return appFirst.load() + appLater.load() >= 10; }, 2000);
        after.disconnect();
        engine.waitForAudioCallbacks();
        check("attaching again adds no second hook",
              engine.audioOut.listenerCount() == outBase + 1 &&
              engine.audioIn.listenerCount() == inBase + 1,
              hooks(engine.audioOut.listenerCount()));
        check("... and keeps the App's subscription (it still runs first)",
              passes && appLater.load() == 0,
              to_string(appFirst.load()) + " passes with the App first, " +
              to_string(appLater.load()) + " with it later");
        internal::detachAppAudio(*app);
        internal::attachAppAudio(*app);
        const int callsAtEnd = p.audioOutCalls.load();
        this_thread::sleep_for(chrono::milliseconds(50));
        check("once detached (an App runs once), an attach subscribes nothing",
              engine.audioOut.listenerCount() == outBase && engine.audioIn.listenerCount() == inBase &&
              p.audioOutCalls.load() == callsAtEnd,
              hooks(engine.audioOut.listenerCount()));
    }

    engine.shutdown();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
