// =============================================================================
// core/tests/entryStacks — regression test for #349: every entry point where
// TrussC calls app code puts the matrix and style stacks back to the depth
// they had when it was entered, and warns naming the entry point.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// Drives the windowed main loop's pieces the way core/tests/frameTiming does
// (beginMainLoopFrame, runIndependentUpdates, runSyncedUpdate), plus
// runHeadlessApp.
//
// Guards:
//   1. The prelude (runOnMainThread work): a push it leaves open is dropped
//      before update, with "the main-thread queue (...) ended with ...".
//   2. update(), synced (beginMainLoopFrame + runSyncedUpdate): dropped at the
//      end of update(), with "update() ended with ...".
//   3. update(), independent: VSYNC update and every fixed-Hz step start at
//      the depth the frame had (leaks don't pile up across steps).
//   4. Restored to the depth at entry, not to 0.
//   5. The App's setup() is its own entry point: its leak is reported as
//      "setup() ended with ...", and update() then starts at depth 0.
//   6. Style values and transforms set outside a push carry on (only the
//      push/pop pairs are contained), and pop order restores the matrix and
//      style that were current at the first leaked push.
//   7. Rate limit per entry point: a repeat in the same entry point within
//      5 s is counted, not logged; another entry point still logs.
//   8. runHeadlessApp: setup(), update() and exit() are entry points too.
//
// Not covered here (needs a window; checked by hand under Xvfb, see the PR):
// the event entry points (_event_cb, secondary windows) and the windowed
// exit (_cleanup_cb).
//
// pushMatrix() also calls sokol_gl, which headless mode doesn't set up:
// sokol_gl ignores the call with no context, but asserts on it in a debug
// build, so this test runs its checks only in a release build (as CI builds
// core/tests).
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}
static void checkn(const char* name, bool ok, long long got) {
    std::printf("%-64s %s  (%lld)\n", name, ok ? "PASS" : "FAIL", got);
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// Warnings from the stack containment (RenderContext's log lines).
static vector<string> g_warnings;
static EventListener g_logListener;
static int countWarnings(const string& needle) {
    int n = 0;
    for (auto& w : g_warnings) if (w.find(needle) != string::npos) ++n;
    return n;
}

static size_t matrixDepth() { return internal::getDefaultContext().getMatrixStackDepth(); }
static size_t styleDepth() { return internal::getDefaultContext().getStyleStackDepth(); }
static bool isIdentity(const Mat4& m) {
    const Mat4 id;
    for (int i = 0; i < 16; ++i) if (m.m[i] != id.m[i]) return false;
    return true;
}

using Clk = chrono::steady_clock;
static Clk::duration secs(double s) { return chrono::round<Clk::duration>(chrono::duration<double>(s)); }

// appUpdateFunc is a plain function pointer: state lives at file scope.
static function<void()> g_onUpdate;
static vector<size_t> g_entryDepths;   // matrix depth at the start of each update
static void leakyUpdate() {
    g_entryDepths.push_back(matrixDepth());
    if (g_onUpdate) g_onUpdate();
}

static const string kLeftOpen = " ended with 1 pushMatrix() and 1 pushStyle() still open (missing pop); dropped";

// Each entry point's warning is rate-limited (first one, then at most every
// 5 s). Every section below uses an entry point once for its first warning,
// so the order of the sections matters: the rate-limit section comes after.

// ---------------------------------------------------------------------------
// 1. The prelude (runOnMainThread / Deliver::Main / console / MCP)
// ---------------------------------------------------------------------------
static void testPrelude(Clk::time_point& t) {
    internal::appUpdateFunc = nullptr;
    setFps(VSYNC);
    g_warnings.clear();
    // Posted from a worker (on the main thread runOnMainThread runs inline),
    // so it runs in the next frame's prelude.
    thread worker([] {
        runOnMainThread([] {
            pushMatrix();
            translate(5, 0);
            pushStyle();
        });
    });
    worker.join();
    checkn("prelude: nothing pushed before the frame", matrixDepth() == 0, (long long)matrixDepth());
    t += secs(1.0 / 60.0);
    internal::beginMainLoopFrame(t);
    checkn("prelude: matrix stack back to its depth at entry (0)", matrixDepth() == 0, (long long)matrixDepth());
    checkn("prelude: style stack back to its depth at entry (0)", styleDepth() == 0, (long long)styleDepth());
    check("prelude: getMatrix() is the matrix before the push (identity)", isIdentity(getMatrix()));
    checkn("prelude: warns naming the main-thread queue",
           countWarnings("the main-thread queue (runOnMainThread / Deliver::Main / console / MCP)" + kLeftOpen) == 1,
           countWarnings("the main-thread queue"));
}

// ---------------------------------------------------------------------------
// 2. update(), synced to draw (setFps)
// ---------------------------------------------------------------------------
static void testSyncedUpdate(Clk::time_point& t) {
    internal::appUpdateFunc = leakyUpdate;
    g_onUpdate = [] { pushMatrix(); translate(3, 0); pushStyle(); };
    setFps(VSYNC);
    g_warnings.clear();
    g_entryDepths.clear();
    t += secs(1.0 / 60.0);
    if (internal::beginMainLoopFrame(t)) internal::runSyncedUpdate();
    checkn("synced update: ran once", g_entryDepths.size() == 1, (long long)g_entryDepths.size());
    checkn("synced update: matrix stack back to 0 after update()", matrixDepth() == 0, (long long)matrixDepth());
    checkn("synced update: style stack back to 0 after update()", styleDepth() == 0, (long long)styleDepth());
    check("synced update: getMatrix() back to identity", isIdentity(getMatrix()));
    checkn("synced update: warns \"update() ended with 1 pushMatrix() ...\"",
           countWarnings("update()" + kLeftOpen) == 1, countWarnings("update()"));
    check("synced update: no other warning", g_warnings.size() == 1);
}

// ---------------------------------------------------------------------------
// 3. update(), independent (setIndependentFps): VSYNC and fixed-Hz steps
// ---------------------------------------------------------------------------
static void testIndependentUpdates(Clk::time_point& t) {
    internal::appUpdateFunc = leakyUpdate;
    g_onUpdate = [] { pushMatrix(); translate(1, 0); pushStyle(); };

    setIndependentFps(VSYNC, EVENT_DRIVEN);
    g_entryDepths.clear();
    for (int i = 0; i < 5; ++i) {
        t += secs(1.0 / 60.0);
        internal::runIndependentUpdates(t);
    }
    bool allZero = g_entryDepths.size() == 5;
    for (size_t d : g_entryDepths) if (d != 0) allZero = false;
    check("independent VSYNC: 5 updates, each starts at depth 0", allZero);
    checkn("independent VSYNC: depth 0 after the updates (no pile-up)", matrixDepth() == 0, (long long)matrixDepth());
    checkn("independent VSYNC: style depth 0 after the updates", styleDepth() == 0, (long long)styleDepth());

    // Fixed Hz: one frame that runs several catch-up steps.
    setIndependentFps(240, EVENT_DRIVEN);
    t += secs(1.0 / 60.0);
    internal::runIndependentUpdates(t);   // first frame after the switch: starts the clock
    g_entryDepths.clear();
    t += secs(4.0 / 240.0 + 0.0001);
    internal::runIndependentUpdates(t);
    allZero = g_entryDepths.size() >= 4;
    for (size_t d : g_entryDepths) if (d != 0) allZero = false;
    checkn("fixed-Hz: every step of a 4-step frame starts at depth 0", allZero, (long long)g_entryDepths.size());
    checkn("fixed-Hz: depth 0 after the steps", matrixDepth() == 0, (long long)matrixDepth());
    check("fixed-Hz: getMatrix() back to identity", isIdentity(getMatrix()));
    setFps(VSYNC);
}

// ---------------------------------------------------------------------------
// 4. Restored to the depth at entry, not to 0
// ---------------------------------------------------------------------------
static void testDepthAtEntry() {
    internal::appUpdateFunc = leakyUpdate;
    g_onUpdate = [] { pushMatrix(); translate(7, 0); pushStyle(); };
    pushMatrix();
    translate(2, 0);
    pushStyle();
    const Mat4 before = getMatrix();
    g_entryDepths.clear();
    internal::runMainUpdate();
    checkn("entry depth: update() entered at depth 1", g_entryDepths.size() == 1 && g_entryDepths[0] == 1,
           g_entryDepths.empty() ? -1 : (long long)g_entryDepths[0]);
    checkn("entry depth: matrix stack back to 1, not 0", matrixDepth() == 1, (long long)matrixDepth());
    checkn("entry depth: style stack back to 1, not 0", styleDepth() == 1, (long long)styleDepth());
    bool same = true;
    for (int i = 0; i < 16; ++i) if (getMatrix().m[i] != before.m[i]) same = false;
    check("entry depth: getMatrix() is the caller's (translate 2), kept", same);
    popStyle();
    popMatrix();
    checkn("entry depth: the caller's own pops still match", matrixDepth() == 0 && styleDepth() == 0,
           (long long)(matrixDepth() + styleDepth()));
}

// ---------------------------------------------------------------------------
// 5. The App's setup() is its own entry point
// ---------------------------------------------------------------------------
static size_t g_setupAppUpdateDepth = 99;
class LeakySetupApp : public App {
public:
    void setup() override { pushMatrix(); translate(4, 0); pushStyle(); }
    void update() override { g_setupAppUpdateDepth = matrixDepth(); }
};
static shared_ptr<LeakySetupApp> g_setupApp;
static void setupAppUpdate() { if (g_setupApp) g_setupApp->handleUpdate(0, 0); }

static void testSetup(Clk::time_point& t) {
    g_setupApp = make_shared<LeakySetupApp>();
    internal::appUpdateFunc = setupAppUpdate;
    setFps(VSYNC);
    g_warnings.clear();
    t += secs(1.0 / 60.0);
    if (internal::beginMainLoopFrame(t)) internal::runSyncedUpdate();   // first update runs setup()
    checkn("setup: the App's update() after it starts at depth 0", g_setupAppUpdateDepth == 0,
           (long long)g_setupAppUpdateDepth);
    checkn("setup: warns \"setup() ended with 1 pushMatrix() ...\"",
           countWarnings("setup()" + kLeftOpen) == 1, countWarnings("setup()"));
    check("setup: no update() warning (the leak was setup()'s)", countWarnings("update() ended") == 0);
    checkn("setup: depth 0 afterwards", matrixDepth() == 0 && styleDepth() == 0,
           (long long)(matrixDepth() + styleDepth()));
    internal::appUpdateFunc = nullptr;
    g_setupApp.reset();
}

// ---------------------------------------------------------------------------
// 6. Only push/pop pairs are contained
// ---------------------------------------------------------------------------
static void testValuesCarry() {
    internal::appUpdateFunc = leakyUpdate;
    // A color and a transform set outside a push carry on; one set inside the
    // leaked push goes with it.
    g_onUpdate = [] {
        setColor(0.25f, 0.5f, 0.75f);
        translate(6, 0);
        pushStyle();
        setColor(1.0f, 0.0f, 0.0f);
        pushMatrix();
        translate(100, 0);
    };
    resetStyle();
    internal::runMainUpdate();
    const Color c = getColor();
    check("values: setColor() outside the push carries into the next entry",
          c.r == 0.25f && c.g == 0.5f && c.b == 0.75f);
    const Mat4 m = getMatrix();
    check("values: translate() outside the push carries (x 6, not 106)", m.m[3] == 6.0f || m.m[12] == 6.0f);
    // Clean up for the next sections: a bare transform is not a stack entry.
    internal::getDefaultContext().resetStacksAtFrameEnd();
    resetStyle();
}

// ---------------------------------------------------------------------------
// 7. Rate limit per entry point
// ---------------------------------------------------------------------------
static void testRateLimit(Clk::time_point& t) {
    internal::appUpdateFunc = leakyUpdate;
    g_onUpdate = [] { pushMatrix(); pushStyle(); };
    setFps(VSYNC);
    g_warnings.clear();
    for (int i = 0; i < 3; ++i) {
        t += secs(1.0 / 60.0);
        if (internal::beginMainLoopFrame(t)) internal::runSyncedUpdate();
    }
    checkn("rate limit: update() leaks within 5 s are counted, not logged",
           countWarnings("update() ended") == 0, countWarnings("update() ended"));
    checkn("rate limit: depth 0 after each (still contained)", matrixDepth() == 0, (long long)matrixDepth());
    // Another entry point has its own limiter: the prelude logged once above,
    // exit() never has.
    {
        internal::EntryStackGuard guard(internal::AppEntry::Exit);
        pushMatrix();
        pushStyle();
    }
    checkn("rate limit: exit() logs on its own limiter",
           countWarnings("exit()" + kLeftOpen) == 1, countWarnings("exit()"));
    internal::appUpdateFunc = nullptr;
}

// ---------------------------------------------------------------------------
// 8. runHeadlessApp
// ---------------------------------------------------------------------------
static size_t g_hUpdateDepthMax = 0;
static int g_hUpdates = 0;
class LeakyHeadlessApp : public App {
public:
    void setup() override { pushMatrix(); }
    void update() override {
        g_hUpdateDepthMax = std::max(g_hUpdateDepthMax, matrixDepth());
        pushMatrix();
        if (++g_hUpdates >= 5) requestExit();
    }
    void exit() override { pushMatrix(); }
};

static void testHeadless() {
    g_warnings.clear();
    runHeadlessApp<LeakyHeadlessApp>(HeadlessSettings().setFps(200));
    checkn("headless: update() always starts at depth 0", g_hUpdateDepthMax == 0, (long long)g_hUpdateDepthMax);
    checkn("headless: depth 0 after the run (setup / update / exit contained)",
           matrixDepth() == 0, (long long)matrixDepth());
}

} // namespace

TC_CORE_TEST_MAIN() {
#ifndef NDEBUG
    std::printf("SKIP: needs a release build (sokol_gl asserts it is set up on pushMatrix())\n");
    return 0;
#else
    getMainThreadId();   // this thread is the main thread (as _setup_cb records it)
    g_logListener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) g_warnings.push_back(e.message);
    });

    Clk::time_point t = Clk::now();
    testPrelude(t);
    testSyncedUpdate(t);
    testIndependentUpdates(t);
    testDepthAtEntry();
    testSetup(t);
    testValuesCarry();
    testRateLimit(t);
    testHeadless();

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
#endif
}
