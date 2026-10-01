// =============================================================================
// core/tests/windowSetApp — behavioral regression test for #318: an App runs
// once, and it ends when it leaves its window, whether the window closes or
// Window::setApp() swaps it out or removes it.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The real AudioEngine runs on miniaudio's null backend
// (internal::setNullAudioBackendForTests()), so an App's audioOut() is called
// on the audio thread without a sound card.
//
// Guards the invariants:
//   - setApp(other) runs the outgoing App's exit() and then cleanup(), once
//     each, and detaches its audioOut() / audioIn(): it is not called again.
//     The window shows the incoming App, whose setup() runs on its first tick.
//   - Attaching the swapped-out App again is refused with one error, and the
//     window keeps its current App; its setup() does not run again.
//   - setApp(nullptr) runs the same teardown.
//   - An App that already ran cleanup() is not ended a second time.
//   - The same from the App's own update() (the window's tick).
//   - An App whose own setup() swaps its window to another App gets no audio
//     hooks afterwards.
//   - An App added as a child with addChild() and destroyed has its audio
//     hooks detached by cleanupTree().
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

template <class Pred>
static bool waitFor(Pred pred, int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 1) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return pred();
}

// An App that counts its lifecycle calls. `order` records them as letters:
// S = setup(), E = exit(), C = cleanup(). In update() / setup() it can ask
// its window for a swap.
struct CountApp : App {
    atomic<int> setups{0}, exits{0}, cleanups{0}, audioCalls{0};
    string order;
    size_t outHooksAtCleanup = 0;
    Window* window = nullptr;           // the window the swaps below act on
    shared_ptr<App> swapInUpdate;       // set: update() calls window->setApp(it)
    bool removeInUpdate = false;        // update() calls window->setApp(nullptr)
    shared_ptr<App> swapInSetup;        // set: setup() calls window->setApp(it)

    void setup() override {
        ++setups;
        order += 'S';
        if (swapInSetup && window) window->setApp(swapInSetup);
    }
    void update() override {
        if (!window) return;
        if (swapInUpdate) {
            auto next = swapInUpdate;
            swapInUpdate.reset();
            window->setApp(next);
        }
        if (removeInUpdate) {
            removeInUpdate = false;
            window->setApp(nullptr);
        }
    }
    void exit() override { ++exits; order += 'E'; }
    void cleanup() override {
        ++cleanups;
        order += 'C';
        outHooksAtCleanup = AudioEngine::getInstance().audioOut.listenerCount();
    }
    void audioOut(AudioOutBuffer&) override { ++audioCalls; }
};

// A Window made in a headless test has no native side: it is a closed window
// (isOpen() is false), and setApp() only takes an open one. OpenWindow stands
// in for an open one: its native_ points at a dummy that nothing here
// dereferences (setApp() and tickTree() don't), and is cleared before
// ~Window() would close() it.
static int g_nativeStandIn = 0;
struct OpenWindow : Window {
    OpenWindow() { native_ = &g_nativeStandIn; }
    ~OpenWindow() { native_ = nullptr; }
};

// One tick of a secondary window, in its own context (as the platform glue
// does): setup() runs on the window's first tick.
static void tickWindow(Window& w) {
    internal::WindowContext* prev = internal::currentWindowCtx();
    internal::currentWindowCtx() = &w.context();
    w.tickTree();
    internal::currentWindowCtx() = prev;
}

// True if `app`'s audioOut() is not called during ~10 audio passes.
static bool audioStopped(CountApp& app) {
    AudioEngine::getInstance().waitForAudioCallbacks();
    const int calls = app.audioCalls.load();
    this_thread::sleep_for(chrono::milliseconds(50));
    return app.audioCalls.load() == calls;
}

// True if `app`'s audioOut() is called a few more times.
static bool audioRuns(CountApp& app) {
    const int calls = app.audioCalls.load();
    return waitFor([&] { return app.audioCalls.load() >= calls + 3; }, 2000);
}

// -----------------------------------------------------------------------------

int main() {
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

    mutex logMutex;
    vector<string> errors;
    EventListener logSub = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Error) return;
        lock_guard<mutex> lock(logMutex);
        errors.push_back(e.message);
    });
    auto countErrors = [&](const string& needle) {
        lock_guard<mutex> lock(logMutex);
        size_t n = 0;
        for (auto& m : errors) if (m.find(needle) != string::npos) ++n;
        return n;
    };

    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = 48000;
    settings.channels = 2;
    settings.bufferSize = 256;
    const bool started = engine.init(settings);
    check("engine starts on the null backend", started && engine.isInitialized());
    if (!started) return 1;
    const size_t outBase = engine.audioOut.listenerCount();
    const size_t inBase = engine.audioIn.listenerCount();
    auto hooksAtBase = [&] {
        return engine.audioOut.listenerCount() == outBase &&
               engine.audioIn.listenerCount() == inBase;
    };
    auto hooks = [&] { return to_string(engine.audioOut.listenerCount()) + " audioOut hooks"; };

    // --- setApp(other) from outside the window, then re-attach, then nullptr -------
    {
        OpenWindow win, other;
        auto a = make_shared<CountApp>();
        auto b = make_shared<CountApp>();
        win.setApp(a);
        tickWindow(win);
        check("a: attached, setup() once, audioOut() called",
              a->setups.load() == 1 && audioRuns(*a));

        win.setApp(b);
        check("swap: the outgoing App's exit() and cleanup() ran once each",
              a->exits.load() == 1 && a->cleanups.load() == 1, a->order);
        check("swap: exit() before cleanup(), its hooks detached after cleanup()",
              a->order == "SEC" && a->outHooksAtCleanup == outBase + 1, a->order);
        check("swap: its audioOut() is not called again", audioStopped(*a) && hooksAtBase(), hooks());
        check("swap: the window shows the incoming App",
              win.getApp() == b && b->setups.load() == 0 && b->exits.load() == 0);
        tickWindow(win);
        check("swap: the incoming App's setup() runs on the next tick",
              b->setups.load() == 1 && audioRuns(*b));

        const size_t refused = countErrors("already ran cleanup()");
        win.setApp(a);
        tickWindow(win);
        check("re-attach to the same window: refused with one error",
              countErrors("already ran cleanup()") == refused + 1);
        check("... the window keeps its current App, which keeps running",
              win.getApp() == b && b->exits.load() == 0 && audioRuns(*b));
        check("... and the refused App runs nothing",
              a->setups.load() == 1 && a->exits.load() == 1 && audioStopped(*a));
        other.setApp(a);
        check("re-attach to another window: refused with one error",
              other.getApp() == nullptr && countErrors("already ran cleanup()") == refused + 2);

        win.setApp(nullptr);
        check("setApp(nullptr): exit() and cleanup() ran once each",
              b->order == "SEC" && b->exits.load() == 1 && b->cleanups.load() == 1, b->order);
        check("setApp(nullptr): its audioOut() is not called again, the window is empty",
              audioStopped(*b) && hooksAtBase() && win.getApp() == nullptr, hooks());
        check("the earlier App was not ended a second time",
              a->exits.load() == 1 && a->cleanups.load() == 1, a->order);
    }

    // --- an App whose cleanup() already ran is not ended twice -------------------
    {
        OpenWindow win;
        auto a = make_shared<CountApp>();
        win.setApp(a);
        tickWindow(win);
        // What the platform Window::close() does with the App before it lets go.
        a->exit();
        a->cleanup();
        internal::detachAppAudio(*a);
        win.setApp(nullptr);
        check("setApp(nullptr) after the App's end: no second exit() / cleanup()",
              a->exits.load() == 1 && a->cleanups.load() == 1, a->order);
    }

    // --- from the App's own update() ---------------------------------------------
    {
        OpenWindow win;
        auto c = make_shared<CountApp>();
        auto d = make_shared<CountApp>();
        c->window = &win;
        d->window = &win;
        win.setApp(c);
        tickWindow(win);
        check("in update(): c set up, audioOut() called", c->setups.load() == 1 && audioRuns(*c));

        c->swapInUpdate = d;
        tickWindow(win);
        check("in update(): setApp(other) ran c's exit() and cleanup() once each",
              c->order == "SEC" && win.getApp() == d, c->order);
        check("in update(): c's audioOut() is not called again", audioStopped(*c), hooks());
        tickWindow(win);
        check("in update(): d is set up on the next tick", d->setups.load() == 1 && audioRuns(*d));

        d->removeInUpdate = true;
        tickWindow(win);
        check("in update(): setApp(nullptr) ran d's exit() and cleanup() once each",
              d->order == "SEC" && win.getApp() == nullptr, d->order);
        check("in update(): d's audioOut() is not called again",
              audioStopped(*d) && hooksAtBase(), hooks());
        tickWindow(win);
        check("in update(): c was not ended a second time", c->order == "SEC", c->order);
    }

    // --- an App whose setup() swaps its window to another App ---------------------
    {
        OpenWindow win;
        auto f = make_shared<CountApp>();
        auto g = make_shared<CountApp>();
        f->window = &win;
        f->swapInSetup = g;
        win.setApp(f);
        tickWindow(win);
        check("in setup(): the swap ends the App (exit(), cleanup())",
              f->order == "SEC" && win.getApp() == g, f->order);
        tickWindow(win);
        check("in setup(): the swapped-out App gets no audio hooks",
              audioStopped(*f) && f->audioCalls.load() == 0 && g->setups.load() == 1 &&
              engine.audioOut.listenerCount() == outBase + 1,
              to_string(f->audioCalls.load()) + " calls, " + hooks());
        win.setApp(nullptr);
        check("in setup(): the incoming App ends on setApp(nullptr)",
              g->order == "SEC" && hooksAtBase(), g->order);
    }

    // --- an App added as a child and destroyed -----------------------------------
    {
        OpenWindow win;
        auto root = make_shared<CountApp>();
        auto child = make_shared<CountApp>();
        win.setApp(root);
        root->addChild(child);
        tickWindow(win);
        check("child App: set up on the tick, audioOut() called",
              child->setups.load() == 1 && audioRuns(*child));
        child->destroy();
        tickWindow(win);
        check("child App: destroy() runs its cleanup()", child->cleanups.load() == 1, child->order);
        check("child App: its audioOut() is not called again",
              audioStopped(*child) && engine.audioOut.listenerCount() == outBase + 1, hooks());
        win.setApp(nullptr);
        check("child App case: no hook left", hooksAtBase(), hooks());
    }

    engine.shutdown();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
