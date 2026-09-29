// =============================================================================
// core/tests/frameTiming — regression test for the time-handling cleanup
// (#229 elapsed clock, #228 fixed-Hz loop, ScreenRecorder pacing).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards:
//   1. One elapsed clock: steady, origin at program start (not first call),
//      monotonic; resetElapsedTimeCounter() is a display offset only.
//   2. getFrameElapsedTime() is constant within a frame (also through the
//      main loop's frame start, beginMainLoopFrame).
//   3. Fixed-Hz update: every step reports the nominal 1/updateFps as dt, at
//      most 10 steps run per frame by default (excess dropped, warned once;
//      setMaxUpdateSteps() sets the cap, <= 0 runs every step), and
//      getFrameRate() reports the measured rate, steady at non-integer ratios.
//      The independent VSYNC update and the default draw-synced update
//      (setFps(VSYNC) / setFps(N), runSyncedUpdate) record their rate too.
//   4. setFps()/setIndependentFps() at runtime do not replay the time spent in
//      the previous mode (neither as fixed steps nor as the first measured
//      dt); the first frame after a switch runs a step / draws;
//      re-applying the current rates (every frame, from update() or draw())
//      changes nothing.
//   5. Fixed-fps draw skip / window throttle: no skipped frames when the
//      target equals or sits just above the display rate; integer ratios
//      unchanged. Also through the main loop's draw decision.
//   6. Node timers are countdowns: callAfter fires on schedule, callEvery keeps
//      its phase and fires once when late (and takes std::bind results /
//      generic lambdas), callEveryCatchUp fires once per due interval up to its
//      limit (cancelling from the callback stops it; fixed-Hz dropped time is
//      not counted), resets don't affect them, and a main-window timer is
//      charged only the time since its creation: one created between updates
//      (after an idle gap or a stall), and one created late in a long update
//      or setup() in a measured-dt mode. A node re-parented during an update
//      (so updated twice in it) is counted down once.
//   7. ScreenRecorder pacing (the pacer's tick(), all ScreenRecorder reads):
//      decimation and PTS are exact after long uptime, and neither the PTS nor
//      the duration cutoff moves with resetElapsedTimeCounter(); neither does
//      tc_get_health's uptime.
//   8. runHeadlessApp: nominal dt, catch-up capped at 10 steps per pass at
//      any rate (with its own warning; setMaxUpdateSteps() applies here too),
//      runOnMainThread not starved, getFrameRate() ~ the target rate, and a
//      fast rate keeps up: the loop sleeps until the next step is due, on a
//      timer that doesn't round up to a Windows timer tick (~15.6 ms).
//
// Most checks drive the loops with simulated time and are exact. The ones
// that depend on real sleeps say so ("Timing-sensitive") and use lower bounds,
// medians or wide margins, so a busy CI runner doesn't fail them.
// =============================================================================

#include <TrussC.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}
static void checkf(const char* name, bool ok, double got) {
    std::printf("%-64s %s  (%.9g)\n", name, ok ? "PASS" : "FAIL", got);
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

static void sleepMs(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

// Count "fell behind" warnings from the loops (all, and the headless loop's).
static int g_dropWarnings = 0;
static int g_headlessDropWarnings = 0;
static EventListener g_logListener;

// ---------------------------------------------------------------------------
// 1-2. Elapsed clock
// ---------------------------------------------------------------------------
static void testElapsedClock() {
    // Origin at program start: nothing has read the clock yet in this process
    // (main() just started), so a lazily-initialized clock would read 0 here.
    sleepMs(120);
    double first = getElapsedTime();
    checkf("clock: origin is program start, not first call", first >= 0.11, first);

    // Monotonic and the whole family agrees.
    double prev = getElapsedTime();
    bool mono = true;
    for (int i = 0; i < 20000; ++i) {
        double t = getElapsedTime();
        if (t < prev) mono = false;
        prev = t;
    }
    check("clock: getElapsedTime() is monotonic", mono);
    double d = getElapsedTime();
    double f = getElapsedTimef();
    double ms = getElapsedTimeMillis() / 1000.0;
    double us = getElapsedTimeMicros() / 1e6;
    // Timing-sensitive: read back to back; separate clocks would differ by the
    // 120 ms above, so the 50 ms margin only absorbs a busy runner's
    // preemption.
    check("clock: getElapsedTimef/Millis/Micros read the same clock",
          fabs(f - d) < 0.05 && fabs(ms - d) < 0.05 && fabs(us - d) < 0.05);

    // Reset = display offset: the getters restart, the framework clock doesn't.
    double upBefore = internal::getUptimeSeconds();
    resetElapsedTimeCounter();
    double afterReset = getElapsedTime();
    // Timing-sensitive: not restarted, they would read >= 0.12 s. The 50 ms
    // margin is for a busy runner preempting the test between the reset and
    // the reads.
    checkf("reset: getElapsedTime() restarts near 0", afterReset >= 0.0 && afterReset < 0.05, afterReset);
    check("reset: Millis/Micros/f restart too",
          getElapsedTimeMillis() < 50 && getElapsedTimeMicros() < 50000 && getElapsedTimef() < 0.05f);
    double upAfter = internal::getUptimeSeconds();
    check("reset: framework uptime is not reset", upAfter >= upBefore && upAfter >= 0.11);
    sleepMs(30);
    double later = getElapsedTime();
    checkf("reset: counts on from the reset", later >= 0.03 && later < 0.5, later);

    // getFrameElapsedTime(): one sample per frame, constant within it.
    auto& ctx = internal::mainWindowContext();
    internal::sampleFrameTime(ctx);
    double fa = getFrameElapsedTime();
    sleepMs(20);
    double fb = getFrameElapsedTime();
    check("frame time: constant within a frame", fa == fb);
    checkf("frame time: matches getElapsedTime() at the frame start",
           fabs(fa - later) < 0.05, fa - later);
    internal::sampleFrameTime(ctx);
    double fc = getFrameElapsedTime();
    checkf("frame time: advances with the next frame", fc - fa >= 0.019, fc - fa);
    resetElapsedTimeCounter();
    checkf("frame time: reset applies immediately", getFrameElapsedTime() < 0.001, getFrameElapsedTime());
}

// ---------------------------------------------------------------------------
// 3. Fixed-step helper
// ---------------------------------------------------------------------------
static void testFixedStepHelper() {
    // setMaxUpdateSteps(): the cap is 10 until the app changes it.
    checkf("setMaxUpdateSteps: the default cap is 10", getMaxUpdateSteps() == 10, getMaxUpdateSteps());
    const double iv = 1.0 / 60.0;
    {
        double acc = 0.0;
        auto r = internal::advanceFixedStep(acc, 3.0, iv, getMaxUpdateSteps());
        check("advanceFixedStep: 3 s at 60 Hz runs at most 10 steps", r.steps == 10);
        check("advanceFixedStep: leaves less than one interval", acc >= 0.0 && acc < iv);
        checkf("advanceFixedStep: reports the dropped time",
               fabs(r.droppedTime - (3.0 - 10 * iv - acc)) < 1e-9, r.droppedTime);
    }
    {
        double acc = 0.0;
        auto r = internal::advanceFixedStep(acc, 3.0, iv, 25);
        check("advanceFixedStep: a cap of 25 runs 25 steps", r.steps == 25 && r.droppedTime > 0.0 && acc < iv);
        for (int cap : {0, -1}) {
            acc = 0.0;
            r = internal::advanceFixedStep(acc, 3.0, iv, cap);
            char name[128];
            snprintf(name, sizeof name, "advanceFixedStep: cap %d runs every step (180), drops nothing", cap);
            checkf(name, abs(r.steps - 180) <= 1 && r.droppedTime == 0.0 && acc < iv, r.steps);
        }
    }
    {
        double acc = 0.0, maxAcc = 0.0;
        int steps = 0, drops = 0;
        for (int i = 0; i < 1000; ++i) {
            auto r = internal::advanceFixedStep(acc, 0.02, iv, getMaxUpdateSteps());
            steps += r.steps;
            if (r.droppedTime > 0.0) ++drops;
            maxAcc = max(maxAcc, acc);
        }
        check("advanceFixedStep: 0.02 s x1000 keeps the accumulator bounded", maxAcc < iv);
        checkf("advanceFixedStep: 0.02 s x1000 runs 1200 +/-1 steps", abs(steps - 1200) <= 1, steps);
        check("advanceFixedStep: no drops within the cap", drops == 0);
    }
    {
        double acc = 0.0;
        auto r = internal::advanceFixedStep(acc, 1.0, 0.0, getMaxUpdateSteps());
        check("advanceFixedStep: interval 0 runs nothing", r.steps == 0 && r.droppedTime == 0.0);
    }
    // Headless pass sleep: until the next step is due, at most 1 ms, so a
    // fast rate stays under the step cap per pass.
    const double inf = numeric_limits<double>::infinity();
    check("headless sleep: at most 1 ms (60 Hz)",
          internal::headlessSleepTime(0.0, iv, 0.0) == internal::headlessMaxSleepTime &&
          internal::headlessMaxSleepTime == 0.001);
    checkf("headless sleep: until the next step is due (10 kHz)",
           fabs(internal::headlessSleepTime(0.00003, 0.0001, 0.0) - 0.00007) < 1e-12,
           internal::headlessSleepTime(0.00003, 0.0001, 0.0));
    checkf("headless sleep: the pass's own time counts",
           fabs(internal::headlessSleepTime(0.0002, 0.001, 0.0005) - 0.0003) < 1e-12,
           internal::headlessSleepTime(0.0002, 0.001, 0.0005));
    check("headless sleep: none when the next step is already due",
          internal::headlessSleepTime(0.0005, 0.001, 0.0008) == 0.0 &&
          internal::headlessSleepTime(0.0, 0.001, 0.002) == 0.0);
    check("headless sleep: an invalid rate sleeps 1 ms",
          internal::headlessSleepTime(0.0, inf, 0.0) == 0.001 &&
          internal::headlessSleepTime(0.0, -1.0, 0.0) == 0.001 &&
          internal::headlessSleepTime(0.0, 0.0, 0.0) == 0.001);
}

// ---------------------------------------------------------------------------
// 3-4. Main loop fixed-Hz update (runIndependentUpdates), mode switches
// ---------------------------------------------------------------------------
using Clk = chrono::steady_clock;
// Round to the nearest tick: truncating 1/60 s to whole nanoseconds would land
// a hair below one interval.
static Clk::duration secs(double s) { return chrono::round<Clk::duration>(chrono::duration<double>(s)); }

// appUpdateFunc is a plain function pointer: state lives at file scope.
static vector<double> g_dts;
static int g_updates = 0;
static function<void()> g_onUpdate;
static bool g_inUpdateSeen = true;
static void countingUpdate() {
    ++g_updates;
    g_dts.push_back(getDeltaTime());
    if (!internal::isInUpdate()) g_inUpdateSeen = false;
    if (g_onUpdate) g_onUpdate();
}

// Run `frames` main-loop frames `step` seconds apart, as _frame_cb does
// (beginMainLoopFrame, then the draw-synced update of a drawn frame); returns
// the updates they ran. `between` runs between frames (e.g. what draw() or an
// event handler does).
static int runFrames(Clk::time_point& t, int frames, double step,
                     const function<void()>& between = nullptr) {
    int before = g_updates;
    for (int f = 0; f < frames; ++f) {
        if (between) between();
        t += secs(step);
        if (internal::beginMainLoopFrame(t)) internal::runSyncedUpdate();
    }
    return g_updates - before;
}

static void testMainLoopUpdates() {
    internal::appUpdateFunc = countingUpdate;
    auto& ctx = internal::mainWindowContext();

    // --- dt per step: setIndependentFps(120, VSYNC) on a 60 Hz display ---
    setIndependentFps(120, VSYNC);
    auto t = Clk::now();
    internal::runIndependentUpdates(t);   // first frame after the switch: starts the clock
    check("fixed Hz: first frame after a switch runs one step, no catch-up", g_updates == 1);
    check("fixed Hz: update() runs marked as inside an update", g_inUpdateSeen && !internal::isInUpdate());
    bool allNominal = true, twoPerFrame = true;
    for (int f = 0; f < 120; ++f) {
        t += secs(1.0 / 60.0);
        int before = g_updates;
        g_dts.clear();
        internal::runIndependentUpdates(t);
        int n = g_updates - before;
        if (f > 0 && n != 2) twoPerFrame = false;   // f == 0 may round to 1 or 2
        for (double dt : g_dts) if (dt != 1.0 / 120.0) allNominal = false;
    }
    check("fixed Hz: every step reports dt = 1/updateFps exactly", allNominal);
    check("fixed Hz: 120 Hz on 60 Hz frames runs 2 steps per frame", twoPerFrame);
    checkf("fixed Hz: getFrameRate() ~ 120 (measured)", fabs(getFrameRate() - 120.0) < 1.0, getFrameRate());

    // --- re-applying the current rates is a no-op (setFps(guiValue) every frame) ---
    g_onUpdate = [] { setIndependentFps(120, VSYNC); };   // from inside every update()
    int n = runFrames(t, 60, 1.0 / 60.0);
    g_onUpdate = nullptr;
    checkf("same rates re-applied inside update(): 120 steps in 60 frames", n == 120, n);
    n = runFrames(t, 60, 1.0 / 60.0, [] { setIndependentFps(120, VSYNC); });   // e.g. from draw()
    checkf("same rates re-applied between frames: 120 steps in 60 frames", n == 120, n);
    setIndependentFps(120, 30);   // only the draw rate changes
    n = runFrames(t, 60, 1.0 / 60.0);
    checkf("changing only the draw rate keeps the update's phase (120 in 60 frames)", n == 120, n);
    setIndependentFps(120, VSYNC);

    // --- 10-step cap after a stall: 3 s gap ---
    int warningsBefore = g_dropWarnings;
    int before = g_updates;
    g_dts.clear();
    t += secs(3.0);
    internal::runIndependentUpdates(t);
    checkf("fixed Hz: a 3 s stall runs at most 10 steps", g_updates - before == 10, g_updates - before);
    check("fixed Hz: dt stays nominal across catch-up steps",
          !g_dts.empty() && g_dts.front() == 1.0 / 120.0 && g_dts.back() == 1.0 / 120.0);
    check("fixed Hz: accumulator below one interval after the cap",
          internal::updateAccumulator >= 0.0 && internal::updateAccumulator < 1.0 / 120.0);
    check("fixed Hz: dropped time warned once", g_dropWarnings == warningsBefore + 1);
    t += secs(3.0);
    internal::runIndependentUpdates(t);
    check("fixed Hz: the warning is one-time", g_dropWarnings == warningsBefore + 1);

    // --- setMaxUpdateSteps(): a custom cap, and <= 0 for no cap ---
    setMaxUpdateSteps(25);
    check("setMaxUpdateSteps: getMaxUpdateSteps() reads back 25", getMaxUpdateSteps() == 25);
    before = g_updates;
    t += secs(3.0);
    internal::runIndependentUpdates(t);
    checkf("setMaxUpdateSteps(25): a 3 s stall runs 25 steps", g_updates - before == 25, g_updates - before);
    for (int cap : {0, -1}) {
        setMaxUpdateSteps(cap);
        before = g_updates;
        g_dts.clear();
        t += secs(3.0);
        internal::runIndependentUpdates(t);
        bool nominal = !g_dts.empty();
        for (double dt : g_dts) if (dt != 1.0 / 120.0) nominal = false;
        char name[128];
        snprintf(name, sizeof name, "setMaxUpdateSteps(%d): a 3 s stall replays every step (360)", cap);
        checkf(name, abs(g_updates - before - 360) <= 1 && nominal, g_updates - before);
        check("setMaxUpdateSteps(<= 0): ...and leaves less than one interval",
              internal::updateAccumulator >= 0.0 && internal::updateAccumulator < 1.0 / 120.0);
    }
    setMaxUpdateSteps(10);

    // --- measured rate when overloaded: frames 200 ms apart at 120 Hz ---
    for (int f = 0; f < 12; ++f) {
        t += secs(0.2);
        internal::runIndependentUpdates(t);
    }
    // 10 steps per 0.2 s frame = 50 Hz measured, although 120 Hz is configured.
    checkf("fixed Hz: getFrameRate() reports the measured rate (50, not 120)",
           fabs(getFrameRate() - 50.0) < 0.5, getFrameRate());

    // --- measured rate at a non-integer ratio: 60 Hz update on a 144 Hz display ---
    // Whole-step counts per 10-frame window would read 57.6 .. 72.
    {
        setIndependentFps(60, VSYNC);
        internal::runIndependentUpdates(t);
        mt19937 rng(11);
        uniform_real_distribution<double> jit(-0.0003, 0.0003);
        const auto t0 = t;
        double lo = 1e9, hi = 0.0;
        int steps = 0;
        for (int f = 1; f <= 144 * 3; ++f) {
            t = t0 + secs(f / 144.0 + jit(rng));
            int b = g_updates;
            internal::runIndependentUpdates(t);
            steps += g_updates - b;
            if (f > 12) { lo = min(lo, getFrameRate()); hi = max(hi, getFrameRate()); }
        }
        checkf("fixed Hz: 60 Hz on 144 Hz runs 180 +/-1 steps in 3 s", abs(steps - 180) <= 1, steps);
        char name[128];
        snprintf(name, sizeof name, "fixed Hz: 60 Hz on 144 Hz reads a steady 60 (%.3f .. %.3f)", lo, hi);
        check(name, lo > 59.9 && hi < 60.1);
    }

    // --- mode switch: no replay of the time spent in the previous mode ---
    setFps(VSYNC);
    // As if the app had been in VSYNC mode for an hour: the fixed-Hz
    // timestamp is an hour old.
    internal::lastUpdateTime = t - secs(3600.0);
    internal::lastUpdateTimeInitialized = true;
    setIndependentFps(60, VSYNC);   // runtime switch
    before = g_updates;
    internal::runIndependentUpdates(t);
    checkf("mode switch: VSYNC -> fixed 60 Hz after 1 h runs 1 step", g_updates - before == 1, g_updates - before);
    n = runFrames(t, 60, 1.0 / 60.0);
    checkf("mode switch: the new rate runs from the switch (60 in 1 s)", abs(n - 60) <= 1, n);

    // Round trip: fixed -> EVENT_DRIVEN (no updates) for an hour -> fixed again.
    setIndependentFps(EVENT_DRIVEN, VSYNC);
    t += secs(3600.0);
    internal::runIndependentUpdates(t);
    setIndependentFps(60, VSYNC);
    before = g_updates;
    internal::runIndependentUpdates(t);
    t += secs(1.0 / 60.0);
    internal::runIndependentUpdates(t);
    checkf("mode switch: round trip does not replay the idle hour (1 + 1 steps)",
           g_updates - before == 2, g_updates - before);

    // Switch from inside update(): the old mode's remaining steps are not run.
    setIndependentFps(120, VSYNC);
    internal::runIndependentUpdates(t);
    static int inCallback = 0;
    g_onUpdate = []() { if (++inCallback == 1) setIndependentFps(30, VSYNC); };
    before = g_updates;
    t += secs(5.0 / 120.0);   // 5 steps pending
    internal::runIndependentUpdates(t);
    checkf("mode switch: from update() stops the frame's old steps", g_updates - before == 1, g_updates - before);
    g_onUpdate = nullptr;

    // --- VSYNC update: one per frame, measured dt, rate recorded ---
    // (Runs after the fixed-Hz part: the first-ever update call would ask sokol
    // for its frame-duration estimate.) Timing-sensitive, lower bounds only:
    // this and the synced part below sleep 10 ms per frame and check that dt
    // is at least that.
    setIndependentFps(VSYNC, VSYNC);
    ctx.rateCount = 0;
    ctx.rateIndex = 0;
    g_dts.clear();
    for (int i = 0; i < 6; ++i) {
        sleepMs(10);
        internal::runIndependentUpdates(Clk::now());
    }
    bool measured = g_dts.size() == 6;
    for (double dt : g_dts) if (dt < 0.0099 || dt > 5.0) measured = false;
    check("VSYNC update: one update per frame with the measured dt (>= 10 ms)", measured);
    checkf("VSYNC update: getFrameRate() recorded (0 < rate <= 100)",
           getFrameRate() > 0.0 && getFrameRate() <= 101.0, getFrameRate());

    // --- the default mode: update synced to draw (setFps(VSYNC)), driven the
    // way _frame_cb drives it: beginMainLoopFrame, then runSyncedUpdate ---
    setFps(VSYNC);
    ctx.rateCount = 0;
    ctx.rateIndex = 0;
    g_dts.clear();
    g_inUpdateSeen = true;
    {
        vector<double> inUpdate, afterUpdate;   // getFrameElapsedTime() per frame
        g_onUpdate = [&inUpdate] { inUpdate.push_back(getFrameElapsedTime()); };
        int frames = 0, drawn = 0;
        before = g_updates;
        for (int i = 0; i < 6; ++i) {
            sleepMs(10);
            ++frames;
            if (internal::beginMainLoopFrame(Clk::now())) {
                ++drawn;
                internal::runSyncedUpdate();
                sleepMs(2);                     // the frame's draw, later on
                afterUpdate.push_back(getFrameElapsedTime());
            }
        }
        g_onUpdate = nullptr;
        checkf("synced VSYNC: every frame draws and runs one update",
               drawn == frames && g_updates - before == frames, g_updates - before);
        bool dtMeasured = g_dts.size() == 6;
        for (double dt : g_dts) if (dt < 0.0099 || dt > 5.0) dtMeasured = false;
        check("synced VSYNC: update() gets the measured dt (>= 10 ms)", dtMeasured);
        check("synced VSYNC: update() runs marked as inside an update",
              g_inUpdateSeen && !internal::isInUpdate());
        checkf("synced VSYNC: getFrameRate() recorded (0 < rate <= 100)",
               getFrameRate() > 0.0 && getFrameRate() <= 101.0, getFrameRate());
        bool constant = inUpdate.size() == 6 && afterUpdate.size() == 6;
        bool advances = constant;
        for (size_t i = 0; constant && i < inUpdate.size(); ++i) {
            if (inUpdate[i] != afterUpdate[i]) constant = false;
            if (i > 0 && inUpdate[i] - inUpdate[i - 1] < 0.0099) advances = false;
        }
        check("synced VSYNC: getFrameElapsedTime() is one value per frame", constant);
        check("synced VSYNC: getFrameElapsedTime() advances with each frame (>= 10 ms)", advances);
    }
    // setFps(30) on a 60 Hz display: every other frame draws and updates.
    setFps(30);
    n = runFrames(t, 60, 1.0 / 60.0);
    checkf("synced 30 fps: 60 display frames run 30 updates", n == 30, n);

    internal::appUpdateFunc = nullptr;
    setFps(VSYNC);
}

// ---------------------------------------------------------------------------
// 4-5. Main loop draw decision (mainLoopShouldDraw)
// ---------------------------------------------------------------------------
static void testMainLoopDraw() {
    mt19937 rng(21);
    uniform_real_distribution<double> jit(-0.0002, 0.0002);

    // setFps(60) on a 60 Hz display, re-applied after every drawn frame (as
    // setFps(guiValue) in update() or draw() does): every tick still draws.
    setFps(60);
    auto t = Clk::now();
    check("draw: the first frame after setFps(60) draws", internal::mainLoopShouldDraw(t));
    const auto t0 = t;
    int draws = 0;
    for (int i = 1; i <= 600; ++i) {
        t = t0 + secs(i / 60.0 + jit(rng));
        if (internal::mainLoopShouldDraw(t)) { ++draws; setFps(60); }
    }
    checkf("draw: setFps(60) re-applied every frame draws 600 of 600 ticks", draws == 600, draws);
    const double acc = internal::drawAccumulator;
    setFps(60);
    check("draw: setFps() with the current rate leaves the draw timing alone",
          internal::lastDrawTimeInitialized && internal::drawAccumulator == acc);

    // A real switch to 30: the first tick after it draws, then every other.
    setFps(30);
    vector<bool> drew;
    const auto t1 = t;
    for (int i = 1; i <= 60; ++i) {
        t = t1 + secs(i / 60.0 + jit(rng));
        drew.push_back(internal::mainLoopShouldDraw(t));
    }
    bool alternates = true;
    for (size_t i = 0; i < drew.size(); ++i) if (drew[i] != (i % 2 == 0)) alternates = false;
    check("draw: after setFps(30) the first tick draws, then every other tick", alternates);

    setFps(VSYNC);
}

// ---------------------------------------------------------------------------
// 5. Draw skip / window throttle decision
// ---------------------------------------------------------------------------
struct SkipStats { int ticks = 0; int draws = 0; int skips = 0; int doubleDraws = 0; int doubleSkips = 0; };

static SkipStats runSkip(double displayHz, double jitterMs, double targetFps, double seconds, unsigned seed) {
    mt19937 rng(seed);
    uniform_real_distribution<double> jit(-jitterMs / 1000.0, jitterMs / 1000.0);
    const double period = 1.0 / displayHz, interval = 1.0 / targetFps;
    double acc = 0.0, prevTick = 0.0;
    bool prevDrew = true;
    SkipStats s;
    const int n = (int)(seconds * displayHz);
    for (int i = 1; i <= n; ++i) {
        double tick = i * period + jit(rng);
        double elapsed = tick - prevTick;
        prevTick = tick;
        bool drew = internal::frameSkipShouldTick(acc, elapsed, interval);
        ++s.ticks;
        if (drew) ++s.draws; else ++s.skips;
        if (i > 1 && drew && prevDrew) ++s.doubleDraws;
        if (i > 1 && !drew && !prevDrew) ++s.doubleSkips;
        prevDrew = drew;
    }
    return s;
}

static void testFrameSkip() {
    const double tenMin = 600.0;
    auto a = runSkip(59.94, 0.2, 60, tenMin, 1);
    checkf("draw skip: 60 fps on 59.94 Hz (+/-0.2 ms, 10 min) skips 0", a.skips == 0, a.skips);
    auto b = runSkip(59.94, 0.5, 60, tenMin, 2);
    checkf("draw skip: 60 fps on 59.94 Hz (+/-0.5 ms, 10 min) skips 0", b.skips == 0, b.skips);
    auto c = runSkip(59.98, 0.1, 60, tenMin, 3);
    checkf("draw skip: 60 fps on 59.98 Hz (+/-0.1 ms, 10 min) skips 0", c.skips == 0, c.skips);
    auto d = runSkip(60.0, 0.1, 60, tenMin, 4);
    checkf("draw skip: 60 fps on 60 Hz (+/-0.1 ms, 10 min) skips 0", d.skips == 0, d.skips);
    auto e = runSkip(60.0, 0.1, 120, 60.0, 5);
    checkf("draw skip: 120 fps on 60 Hz draws every tick", e.skips == 0, e.skips);

    auto g = runSkip(120.0, 0.2, 60, 60.0, 6);
    check("draw skip: 120 Hz -> 60 draws exactly every other tick",
          g.doubleDraws == 0 && g.doubleSkips == 0 && abs(g.draws - g.ticks / 2) <= 1);
    auto h = runSkip(60.0, 0.2, 30, 60.0, 7);
    check("draw skip: 60 Hz -> 30 draws exactly every other tick",
          h.doubleDraws == 0 && h.doubleSkips == 0 && abs(h.draws - h.ticks / 2) <= 1);
    auto k = runSkip(144.0, 0.2, 60, 60.0, 8);
    checkf("draw skip: 144 Hz -> 60 gives 60 x seconds +/-1 draws", abs(k.draws - 3600) <= 1, k.draws);
    auto m = runSkip(60.0, 0.2, 58, 60.0, 9);
    checkf("draw skip: 58 fps on 60 Hz stays near 58 fps", fabs(m.draws / 60.0 - 58.0) < 0.6, m.draws / 60.0);

    // A long stall draws once and does not burst afterwards.
    double acc = 0.0;
    bool drew = internal::frameSkipShouldTick(acc, 2.0, 1.0 / 30.0);
    bool next = internal::frameSkipShouldTick(acc, 1.0 / 60.0, 1.0 / 30.0);
    check("draw skip: after a stall draws once, no burst", drew && !next);
}

// ---------------------------------------------------------------------------
// 6. Node timers
// ---------------------------------------------------------------------------
struct TimerNode : Node {
    void step() { processTimers(); }
};

// One simulated update of the main window (a measured-dt update, as in VSYNC /
// setFps() modes): advance the update count, set dt and the update time (the
// simulated clock advanced by `advance`, default dt), and run the node's
// timers inside the update. `during` runs inside the update before the node's
// timers (like a parent's update() or setup()).
static Clk::time_point g_simNow;
static void simUpdate(TimerNode& n, double dt, double advance = -1.0,
                      const function<void()>& during = nullptr) {
    internal::updateFrameCount++;
    auto& ctx = internal::mainWindowContext();
    ctx.updateDeltaTime = dt;
    g_simNow += secs(advance >= 0.0 ? advance : dt);
    ctx.updateTime = g_simNow;
    ctx.fixedStepUpdate = false;
    ctx.inUpdate = true;
    if (during) during();
    n.step();
    ctx.inUpdate = false;
}
// A timer created between updates counts from its creation time. Anchor the
// simulated clock at "now" right after creating one, so the next update is
// exactly `advance` after it.
static void anchorSimClock() { g_simNow = Clk::now(); }

static void testNodeTimers() {
    const double dt = 1.0 / 60.0;
    auto node = make_shared<TimerNode>();
    anchorSimClock();

    // Created during an update: counts from the next one, so callAfter(1.0)
    // at a fixed 60 Hz fires on exactly the 60th update after it.
    {
        int firedAt = -1, step = 0;
        simUpdate(*node, dt, -1.0, [&] { node->callAfter(1.0, [&] { firedAt = step; }); });
        for (step = 1; step <= 70 && firedAt < 0; ++step) simUpdate(*node, dt);
        checkf("timers: callAfter(1.0) created in an update fires on update 60", firedAt == 60, firedAt);
    }
    // Created between updates, right before one that is a whole dt later.
    {
        int firedAt = -1, step = 0;
        node->callAfter(1.0, [&] { firedAt = step; });
        anchorSimClock();
        for (step = 1; step <= 70 && firedAt < 0; ++step) simUpdate(*node, dt);
        checkf("timers: callAfter(1.0) created between updates fires on update 60", firedAt == 60, firedAt);
    }
    // Created during the current update (e.g. from setup() / a parent's
    // update()): it starts counting next update, so it can't fire early.
    {
        int fired = 0;
        simUpdate(*node, dt, -1.0, [&] { node->callAfter(dt * 1.5, [&] { ++fired; }); });
        check("timers: not counted down in the update it was created in", fired == 0);
        simUpdate(*node, dt);
        check("timers: ...and not fired a frame early", fired == 0);
        simUpdate(*node, dt);
        check("timers: fires once its delay has elapsed", fired == 1);
    }
    // Created between updates after a 10 s idle gap (EVENT_DRIVEN, or a stall):
    // the next update's dt covers the gap, but only the time since creation
    // counts (#228 follow-up; it used to fire on that update, 1 s early).
    {
        int fired = 0;
        node->callAfter(1.0, [&] { ++fired; });
        anchorSimClock();
        simUpdate(*node, 10.001, 0.001);    // redraw() 1 ms later: dt = the whole idle gap
        check("timers: an idle gap before creation is not charged", fired == 0);
        simUpdate(*node, 0.5);
        check("timers: ...not fired before its delay (0.501 s)", fired == 0);
        simUpdate(*node, 0.5);
        check("timers: ...fires once its delay has elapsed (1.001 s)", fired == 1);
    }
    // callEvery keeps its phase: 0.02 s at 60 fps runs at 50 Hz (not 30 Hz).
    {
        int fired = 0;
        uint64_t id = node->callEvery(0.02, [&] { ++fired; });
        anchorSimClock();
        for (int i = 0; i < 360; ++i) simUpdate(*node, dt);   // 6 s
        node->cancelTimer(id);
        checkf("timers: callEvery(0.02) at 60 fps fires 300 +/-1 times in 6 s", abs(fired - 300) <= 1, fired);
    }
    // A late callEvery fires once; callEveryCatchUp once per interval that
    // came due, keeping the phase.
    {
        int every = 0, catchUp = 0;
        uint64_t a = node->callEvery(0.1, [&] { ++every; });
        uint64_t b = node->callEveryCatchUp(0.1, [&] { ++catchUp; });
        anchorSimClock();
        simUpdate(*node, 0.0);              // start counting
        simUpdate(*node, 0.35);             // due at 0.1, 0.2 and 0.3
        check("timers: a late callEvery fires once", every == 1);
        checkf("timers: a late callEveryCatchUp fires once per due interval (3)", catchUp == 3, catchUp);
        simUpdate(*node, 0.05);             // 0.40: due again, in phase
        check("timers: ...and both keep the phase (due again at 0.40)", every == 2 && catchUp == 4);
        node->cancelTimer(a);
        node->cancelTimer(b);
    }
    // No limit by default: a 10 s stall in a measured-dt loop (VSYNC,
    // setFps()) makes it fire for the whole stall in one update.
    {
        int calls = 0;
        uint64_t id = node->callEveryCatchUp(0.01, [&] { ++calls; });
        anchorSimClock();
        simUpdate(*node, 0.0);
        simUpdate(*node, 10.0);
        node->cancelTimer(id);
        checkf("timers: callEveryCatchUp without a limit fires 1000 times for 10 s",
               abs(calls - 1000) <= 1, calls);
    }
    // maxCatchUp: at most that many calls per update; the rest of the due
    // intervals are dropped and the phase is kept.
    {
        int calls = 0;
        uint64_t id = node->callEveryCatchUp(0.1, [&] { ++calls; }, 3);
        anchorSimClock();
        simUpdate(*node, 0.0);
        simUpdate(*node, 0.95);             // 9 due (0.1 .. 0.9)
        checkf("timers: callEveryCatchUp(maxCatchUp = 3) fires 3 of 9 due", calls == 3, calls);
        simUpdate(*node, 0.04);             // 0.99: not due
        check("timers: ...drops the rest (nothing due at 0.99)", calls == 3);
        simUpdate(*node, 0.01);             // 1.00: next due in the old phase
        check("timers: ...and keeps the phase (due again at 1.00)", calls == 4);
        node->cancelTimer(id);
    }
    // Cancelling the timer from its callback stops the remaining calls, also
    // when the callback adds timers (the timer vector reallocates).
    {
        int calls = 0;
        uint64_t id = 0;
        id = node->callEveryCatchUp(0.1, [&] {
            ++calls;
            for (int i = 0; i < 64; ++i) node->callAfter(100.0, [] {});
            if (calls == 2) node->cancelTimer(id);
        });
        anchorSimClock();
        simUpdate(*node, 0.0);
        simUpdate(*node, 0.55);             // 5 due
        checkf("timers: callEveryCatchUp cancelled by its callback stops (2 of 5)", calls == 2, calls);
        simUpdate(*node, 0.1);
        check("timers: ...and stays cancelled", calls == 2);
        node->cancelAllTimers();
    }
    // callEvery takes any callable that can be called with no arguments,
    // including ones that also accept an int (std::bind results, generic
    // lambdas): it has no overload those would make ambiguous. Compile check,
    // and they run as plain callEvery timers.
    {
        struct Ticker { int n = 0; void tick() { ++n; } } ticker;
        int generic = 0;
        uint64_t a = node->callEvery(0.1, bind(&Ticker::tick, &ticker));
        uint64_t b = node->callEvery(0.1, [&](auto&&...) { ++generic; });
        anchorSimClock();
        simUpdate(*node, 0.0);
        simUpdate(*node, 0.1);
        node->cancelTimer(a);
        node->cancelTimer(b);
        check("timers: callEvery takes std::bind results and generic lambdas",
              ticker.n == 1 && generic == 1);
    }
    // resetElapsedTimeCounter() does not move timers.
    {
        int fired = 0;
        node->callAfter(0.1, [&] { ++fired; });
        anchorSimClock();
        simUpdate(*node, 0.0);
        resetElapsedTimeCounter();
        for (int i = 0; i < 6; ++i) simUpdate(*node, dt);
        resetElapsedTimeCounter();
        check("timers: resetElapsedTimeCounter() does not delay a timer", fired == 1);
    }
    internal::mainWindowContext().updateDeltaTime = 0.0;
}

// Node timers through the real main-loop wiring (runIndependentUpdates sets
// dt, the step's time and the in-update mark): fixed 120 Hz update.
static TimerNode* g_loopNode = nullptr;
static function<void()> g_loopDuring;
static void timerLoopUpdate() {
    internal::updateFrameCount++;           // as the app's update does
    ++g_updates;
    if (g_loopDuring) g_loopDuring();
    g_loopNode->step();
}

static void testNodeTimersInLoop() {
    auto node = make_shared<TimerNode>();
    g_loopNode = node.get();
    internal::appUpdateFunc = timerLoopUpdate;
    setIndependentFps(120, VSYNC);
    auto t = Clk::now();
    internal::runIndependentUpdates(t);

    // Created inside a fixed step: fires on exactly the 120th step after it.
    {
        int fired = 0, stepsAfter = 0;
        bool created = false;
        g_loopDuring = [&] {
            if (created) { if (!fired) ++stepsAfter; return; }
            created = true;
            node->callAfter(1.0, [&] { ++fired; });
        };
        runFrames(t, 70, 1.0 / 60.0);
        g_loopDuring = nullptr;
        checkf("loop timers: callAfter(1.0) created in a 120 Hz step fires on step 120",
               fired == 1 && stepsAfter == 120, stepsAfter);
    }
    // Created between frames after a 3 s stall (e.g. a key handler right
    // after a blocking dialog): the catch-up steps of the next frame cover
    // time before the timer existed and must not count.
    {
        int fired = 0;
        // Each step's time on the loop's timeline (what the timers count from).
        Clk::time_point prevStep{}, lastStep{};
        g_loopDuring = [&] { prevStep = lastStep; lastStep = internal::getUpdateTime(); };
        internal::lastUpdateTime = Clk::now() - secs(3.0);   // the previous frame, 3 s ago
        const auto createdFrom = Clk::now();
        node->callAfter(0.05, [&] { ++fired; });
        const auto createdTo = Clk::now();
        t = createdTo + secs(0.001);
        int before = g_updates;
        internal::runIndependentUpdates(t);
        checkf("loop timers: a stall before creation is not charged (10 capped steps)",
               fired == 0 && g_updates - before == 10, g_updates - before);
        int steps = 0;
        while (!fired && steps < 100) { steps += runFrames(t, 1, 1.0 / 120.0); }
        g_loopDuring = nullptr;
        // Fires on the first step at least 0.05 s after its creation: not
        // before (the step it fires on is that late), not after (the step
        // before it was not).
        const double firedAfter = chrono::duration<double>(lastStep - createdFrom).count();
        const double prevAfter = chrono::duration<double>(prevStep - createdTo).count();
        checkf("loop timers: ...then fires on the first step 0.05 s after its creation",
               fired == 1 && steps >= 6 && steps <= 7 && firedAfter >= 0.05 - 1e-6 && prevAfter < 0.05,
               firedAfter);
    }
    // callEveryCatchUp counts step time: after a 3 s stall the frame runs 10
    // capped steps, and a 480 Hz catch-up timer on 120 Hz steps catches up
    // those 10 steps (4 calls each), not the 3 s the loop dropped.
    {
        int calls = 0, perFrame = -1;
        bool created = false;
        g_loopDuring = [&] {
            if (created) return;
            created = true;
            node->callEveryCatchUp(1.0 / 480.0, [&] { ++calls; });
        };
        runFrames(t, 2, 1.0 / 60.0);        // created, then counting
        g_loopDuring = nullptr;
        calls = 0;
        runFrames(t, 1, 1.0 / 60.0);
        perFrame = calls;
        calls = 0;
        internal::lastUpdateTime = t - secs(3.0);   // the previous frame, 3 s ago
        const int before = g_updates;
        internal::runIndependentUpdates(t);
        const int steps = g_updates - before;
        node->cancelAllTimers();
        checkf("loop timers: callEveryCatchUp(1/480) at 120 Hz runs 8 calls per 60 Hz frame",
               perFrame == 8, perFrame);
        checkf("loop timers: ...after a 3 s stall, 4 calls per capped step (40), not 1440",
               steps == 10 && calls == 40, calls);
    }
    // Created in step 1 of a capped catch-up frame (after a 3 s stall): a
    // timer made during a fixed step counts whole steps from the next step,
    // so callAfter(5/120) fires on step 6 of the same frame. The frame is
    // taken at the real clock, so all its steps' nominal times lie before the
    // creation: counted from its creation time instead, it would wait for a
    // later frame.
    {
        const int k = 5;
        int step = 0, firedOn = -1;
        g_loopDuring = [&] {
            if (++step == 1) node->callAfter(k / 120.0, [&] { firedOn = step; });
        };
        const auto now = Clk::now();
        internal::lastUpdateTime = now - secs(3.0);
        const int before = g_updates;
        internal::runIndependentUpdates(now);
        g_loopDuring = nullptr;
        char name[160];
        snprintf(name, sizeof name,
                 "loop timers: callAfter(%d/120) made in step 1 of a capped frame fires on its step %d", k, k + 1);
        checkf(name, g_updates - before == 10 && firedOn == k + 1, firedOn);
        node->cancelAllTimers();
    }
    internal::appUpdateFunc = nullptr;
    g_loopNode = nullptr;
    setFps(VSYNC);
    internal::mainWindowContext().updateDeltaTime = 0.0;
}

// ---------------------------------------------------------------------------
// 6b. A timer created during a measured update counts from its creation
// ---------------------------------------------------------------------------
// In VSYNC / setFps() modes an update's dt is the wall time since the previous
// update, so the update after a long one (a long setup(), a synchronous load
// in update()) reports all of it. A timer created at the end of the long
// update must be charged only the time after its creation, or it fires on
// the next update. Timing-sensitive (real 300 ms sleeps), but every bound is
// derived from the timestamps taken around the creation, so a preempted
// runner can't fail it.
static shared_ptr<App> g_treeRoot;
static void treeUpdate() {
    internal::updateFrameCount++;           // as the app's update does
    ++g_updates;
    g_treeRoot->handleUpdate(0, 0);
}

struct LongSetupNode : Node {
    int fired = 0;
    Clk::time_point createdFrom, createdTo;
    void setup() override {
        sleepMs(300);                       // e.g. a synchronous load
        createdFrom = Clk::now();
        callAfter(0.2, [this] { ++fired; });
        createdTo = Clk::now();
    }
};

static double secsBetween(Clk::time_point a, Clk::time_point b) {
    return chrono::duration<double>(b - a).count();
}

static void testTimersCountFromCreation() {
    setFps(VSYNC);                          // measured dt, update synced to draw
    auto node = make_shared<TimerNode>();
    g_loopNode = node.get();
    internal::appUpdateFunc = timerLoopUpdate;

    // An update that runs 300 ms before it creates callAfter(0.1) and
    // callEveryCatchUp(0.05).
    {
        const double iv = 0.05;
        int after = 0, catchUp = 0;
        Clk::time_point from, to;
        bool created = false;
        g_loopDuring = [&] {
            if (created) return;
            created = true;
            sleepMs(300);
            from = Clk::now();
            node->callAfter(0.1, [&] { ++after; });
            node->callEveryCatchUp(iv, [&] { ++catchUp; });
            to = Clk::now();
        };
        internal::runSyncedUpdate();        // the long update that creates them
        g_loopDuring = nullptr;
        internal::runSyncedUpdate();        // dt covers the whole long update
        const double dt = getDeltaTime();
        const double since2 = secsBetween(from, internal::getUpdateTime());
        checkf("measured dt: the update after a 300 ms update reports it (>= 0.3 s)", dt >= 0.299, dt);
        checkf("measured dt: callAfter(0.1) created at its end doesn't fire on the next update",
               after == 0 || since2 >= 0.1 - 1e-6, since2);
        checkf("measured dt: callEveryCatchUp(0.05) created there doesn't catch up the 300 ms",
               catchUp <= (int)floor(since2 / iv + 1e-6), catchUp);
        sleepMs(150);
        internal::runSyncedUpdate();
        const auto u3 = internal::getUpdateTime();
        check("measured dt: ...callAfter fires once 0.1 s have passed since its creation", after == 1);
        const int lo = (int)floor(secsBetween(to, u3) / iv), hi = (int)floor(secsBetween(from, u3) / iv + 1e-6);
        char name[160];
        snprintf(name, sizeof name,
                 "measured dt: ...callEveryCatchUp caught up the time since its creation only (%d .. %d)", lo, hi);
        checkf(name, catchUp >= lo && catchUp <= hi && catchUp >= 3, catchUp);
        node->cancelAllTimers();
    }

    // A long setup(): Node::setup() (and App::setup()) runs inside the first
    // update, so the next update's dt covers it.
    {
        g_treeRoot = make_shared<App>();
        auto n = make_shared<LongSetupNode>();
        g_treeRoot->addChild(n);
        internal::appUpdateFunc = treeUpdate;
        internal::runSyncedUpdate();        // setup(): 300 ms, then callAfter(0.2)
        internal::runSyncedUpdate();
        const double dt = getDeltaTime();
        Clk::time_point prevU = internal::getUpdateTime(), lastU = prevU;
        checkf("long setup(): the next update's dt covers it (>= 0.3 s)", dt >= 0.299, dt);
        checkf("long setup(): callAfter(0.2) made at its end doesn't fire on the next update",
               n->fired == 0 || secsBetween(n->createdFrom, lastU) >= 0.2 - 1e-6,
               secsBetween(n->createdFrom, lastU));
        for (int f = 0; f < 40 && !n->fired; ++f) {
            sleepMs(50);
            internal::runSyncedUpdate();
            prevU = lastU;
            lastU = internal::getUpdateTime();
        }
        checkf("long setup(): ...then fires on the first update 0.2 s after its creation",
               n->fired == 1 && secsBetween(n->createdFrom, lastU) >= 0.2 - 1e-6 &&
               secsBetween(n->createdTo, prevU) < 0.2,
               secsBetween(n->createdFrom, lastU));
        g_treeRoot.reset();
    }

    internal::appUpdateFunc = nullptr;
    g_loopNode = nullptr;
}

// ---------------------------------------------------------------------------
// 6c. A mode switch restarts the measured delta (guard 4)
// ---------------------------------------------------------------------------
// A switch must not replay the previous mode as the first measured dt, but
// must keep the time that belongs to the new one. Between updates (a key
// handler, draw()) the baseline moves to the switch: after an hour of
// EVENT_DRIVEN idle, setFps(VSYNC) must not report the hour (every Node timer
// would count it at once), but the wait for the first redraw() after
// switching to EVENT_DRIVEN counts. Inside an update the baseline stays at
// that update's start, so work done in it counts whether it comes before or
// after the call (a load in setup(), a rebuild in update()). Timing-sensitive
// (real sleeps), with bounds taken from timestamps or lower bounds only.
struct SwitchingSetupNode : Node {
    int fired = 0;
    void setup() override {
        setFps(60);                         // a real switch (from VSYNC)
        callAfter(0.2, [this] { ++fired; });
        sleepMs(300);                       // e.g. loading assets
    }
};

struct LoadThenSwitchSetupNode : Node {
    int fired = 0;
    void setup() override {
        callAfter(0.2, [this] { ++fired; });
        sleepMs(300);                       // e.g. loading assets
        setFps(60);                         // the switch comes last
    }
};

static void testModeSwitchMeasuredDelta() {
    setFps(VSYNC);
    auto node = make_shared<TimerNode>();
    g_loopNode = node.get();
    internal::appUpdateFunc = timerLoopUpdate;
    int fired = 0;
    bool created = false;
    g_loopDuring = [&] {
        if (created) return;
        created = true;
        node->callAfter(1.0, [&] { ++fired; });
    };
    internal::runSyncedUpdate();            // creates the timer
    g_loopDuring = nullptr;
    internal::runSyncedUpdate();            // it is counting now
    setIndependentFps(EVENT_DRIVEN, VSYNC);
    internal::runIndependentUpdates(Clk::now());   // no update runs
    // As if that last update had been an hour ago.
    internal::mainWindowContext().mainUpdateCallTime -= secs(3600.0);
    setFps(VSYNC);
    internal::runSyncedUpdate();
    const double dt = getDeltaTime();
    checkf("mode switch: the first dt after an hour of EVENT_DRIVEN counts from the switch (< 0.1 s)",
           dt >= 0.0 && dt < 0.1, dt);
    check("mode switch: ...so no timer fires early", fired == 0);
    sleepMs(20);
    internal::runSyncedUpdate();
    checkf("mode switch: the next dt is measured again (>= 20 ms)",
           getDeltaTime() >= 0.0199 && getDeltaTime() < 5.0, getDeltaTime());
    node->cancelAllTimers();

    // setFps() then a 300 ms load in setup() (inside the first update): the
    // next update's dt covers the load, and callAfter(0.2) made after the
    // switch fires on it.
    {
        g_treeRoot = make_shared<App>();
        auto n = make_shared<SwitchingSetupNode>();
        g_treeRoot->addChild(n);
        internal::appUpdateFunc = treeUpdate;
        internal::runSyncedUpdate();        // setup(): setFps(60), callAfter(0.2), 300 ms
        internal::runSyncedUpdate();
        const double dt2 = getDeltaTime();
        checkf("mode switch: setFps() then a 300 ms load in setup(): the next dt covers the load",
               dt2 >= 0.299, dt2);
        check("mode switch: ...and callAfter(0.2) made after setFps() fires on that update", n->fired == 1);
        g_treeRoot.reset();
        internal::appUpdateFunc = timerLoopUpdate;
        setFps(VSYNC);
    }

    // VSYNC -> setFps(EVENT_DRIVEN), then the first redraw() 50 ms later:
    // its dt is the 50 ms wait.
    internal::runSyncedUpdate();
    setFps(EVENT_DRIVEN);
    sleepMs(50);
    internal::runSyncedUpdate();            // the redraw()'s update
    checkf("mode switch: the first update after setFps(EVENT_DRIVEN) counts the wait (>= 50 ms)",
           getDeltaTime() >= 0.0499, getDeltaTime());

    // setFps() from inside an independent VSYNC update: the synced update of
    // the same frame (runSyncedUpdate, as _frame_cb runs it) counts from that
    // update's start, not a whole frame (sokol's estimate).
    {
        setIndependentFps(VSYNC, VSYNC);
        internal::runIndependentUpdates(Clk::now());
        Clk::time_point updateStart;
        bool switched = false;
        g_loopDuring = [&] {
            if (switched) return;
            switched = true;
            updateStart = internal::getUpdateTime();
            setFps(VSYNC);
        };
        sleepMs(20);
        internal::runIndependentUpdates(Clk::now());   // switches from inside update()
        g_loopDuring = nullptr;
        internal::runSyncedUpdate();
        const double dt3 = getDeltaTime();
        const double since = secsBetween(updateStart, internal::getUpdateTime());
        checkf("mode switch: setFps() inside update(): that frame's synced update counts from the update's start",
               dt3 >= 0.0 && dt3 <= since + 1e-6, dt3);
    }

    // The switch at the end of a long setup(): callAfter(0.2), a 300 ms load,
    // then setFps(60). The next update counts the whole setup() and fires the
    // timer, as before the switch existed.
    {
        setFps(VSYNC);
        g_treeRoot = make_shared<App>();
        auto n = make_shared<LoadThenSwitchSetupNode>();
        g_treeRoot->addChild(n);
        internal::appUpdateFunc = treeUpdate;
        internal::runSyncedUpdate();        // setup(): callAfter(0.2), 300 ms, setFps(60)
        internal::runSyncedUpdate();
        checkf("mode switch: callAfter, a 300 ms load, then setFps() in setup(): the next dt covers the load",
               getDeltaTime() >= 0.299, getDeltaTime());
        check("mode switch: ...and the timer fires on it", n->fired == 1);
        g_treeRoot.reset();
        internal::appUpdateFunc = timerLoopUpdate;
    }

    // update() { rebuild 150 ms; setFps(30); }: a timer with 0.1 s left is
    // not pushed back by the rebuild.
    {
        setFps(VSYNC);
        int fired = 0;
        bool created = false, rebuild = false;
        g_loopDuring = [&] {
            if (!created) {
                created = true;
                node->callAfter(0.1, [&] { ++fired; });
                return;
            }
            if (rebuild) {
                rebuild = false;
                sleepMs(150);
                setFps(30);
            }
        };
        internal::runSyncedUpdate();        // creates the timer
        internal::runSyncedUpdate();        // it starts counting (~0.1 s left)
        rebuild = true;
        internal::runSyncedUpdate();        // 150 ms rebuild, then setFps(30)
        const int firedBefore = fired;
        internal::runSyncedUpdate();
        g_loopDuring = nullptr;
        checkf("mode switch: setFps() after a 150 ms rebuild in update(): the next dt covers the rebuild",
               getDeltaTime() >= 0.149, getDeltaTime());
        check("mode switch: ...and a timer with 0.1 s left fires on it", firedBefore == 0 && fired == 1);
        node->cancelAllTimers();
    }

    // Switching from a fixed-Hz step to a measured mode: a timer created later
    // in that step counts from its creation, like any timer created in a
    // measured update (the step's fixed-step mark ends with the switch).
    {
        setIndependentFps(120, VSYNC);
        internal::runIndependentUpdates(Clk::now());   // one step (just switched)
        int fired = 0;
        bool done = false;
        Clk::time_point from;
        g_loopDuring = [&] {
            if (done) return;
            done = true;
            sleepMs(300);
            setFps(VSYNC);                  // to a measured mode, inside the step
            from = Clk::now();
            node->callAfter(0.2, [&] { ++fired; });
        };
        sleepMs(10);                        // a step is due
        internal::runIndependentUpdates(Clk::now());
        g_loopDuring = nullptr;
        internal::runSyncedUpdate();        // the same frame's synced update: dt covers the step
        const double dt4 = getDeltaTime();
        const double since = secsBetween(from, internal::getUpdateTime());
        checkf("mode switch: fixed step -> VSYNC, then a 300 ms step: the synced update's dt covers it",
               dt4 >= 0.299, dt4);
        checkf("mode switch: ...a timer created after the switch in that step counts from its creation",
               fired == 0 || since >= 0.2 - 1e-6, since);
        node->cancelAllTimers();
    }

    // Once the loop runs, a switch between updates sets the first baseline
    // when there is none yet (setup() running in the first draw() of an
    // EVENT_DRIVEN update), instead of leaving the first update to sokol's
    // estimate.
    {
        setFps(VSYNC);
        internal::mainWindowContext().mainUpdateCallTimeInitialized = false;   // no update ran yet
        setFps(60);
        sleepMs(50);
        internal::runSyncedUpdate();
        checkf("mode switch: before the first update, a switch sets the baseline (dt >= 50 ms)",
               getDeltaTime() >= 0.0499, getDeltaTime());
        setFps(VSYNC);
    }

    internal::appUpdateFunc = nullptr;
    g_loopNode = nullptr;
}

// ---------------------------------------------------------------------------
// 6d. A node re-parented during an update is counted down once
// ---------------------------------------------------------------------------
// A node that moves itself under a parent traversed later in the same update
// runs updateTree() twice in it; its timers must not count that update twice
// (fire a frame early).
struct MovingNode : Node {
    shared_ptr<Node> target;
    bool moveNow = false;
    int fired = 0;
    void update() override {
        if (moveNow) {
            moveNow = false;
            target->addChild(shared_from_this());
        }
    }
};

static void testReparentChargesOnce() {
    auto& ctx = internal::mainWindowContext();
    auto root = make_shared<App>();
    auto p1 = make_shared<Node>(), p2 = make_shared<Node>();
    auto n = make_shared<MovingNode>();
    root->addChild(p1);
    root->addChild(p2);
    p1->addChild(n);
    n->target = p2;
    const double dt = 1.0 / 60.0;
    // One simulated measured-dt update of the whole tree (as simUpdate does).
    auto update = [&] {
        internal::updateFrameCount++;
        ctx.updateDeltaTime = dt;
        g_simNow += secs(dt);
        ctx.updateTime = g_simNow;
        ctx.fixedStepUpdate = false;
        ctx.inUpdate = true;
        root->handleUpdate(0, 0);
        ctx.inUpdate = false;
    };
    n->callAfter(3 * dt, [&] { ++n->fired; });
    anchorSimClock();
    update();                               // 2 dt left
    n->moveNow = true;
    update();                               // under p1, then again under p2
    check("re-parent: the node was updated under its new parent", n->getParent() == p2);
    check("re-parent: its timer is counted down once in that update (not fired early)", n->fired == 0);
    update();
    check("re-parent: ...and fires on schedule", n->fired == 1);
    ctx.updateDeltaTime = 0.0;
}

// ---------------------------------------------------------------------------
// 7. ScreenRecorder pacing
// ---------------------------------------------------------------------------
struct PaceResult { int captures = 0; bool ptsIncreasing = true; double minDelta = 1e9, maxDelta = 0; vector<double> pts; };

static PaceResult pace(double start, double sourceHz, double targetFps, double seconds) {
    internal::ScreenRecorderPacer p;
    p.start(start, targetFps, 0.0);
    PaceResult r;
    double last = -1.0;
    const int n = (int)llround(seconds * sourceHz);
    for (int i = 0; i < n; ++i) {
        double now = start + i / sourceHz;
        auto pts = p.onFrame(now);
        if (!pts) continue;
        ++r.captures;
        if (last >= 0.0) {
            double d = *pts - last;
            if (d <= 0.0) r.ptsIncreasing = false;
            r.minDelta = min(r.minDelta, d);
            r.maxDelta = max(r.maxDelta, d);
        }
        last = *pts;
        r.pts.push_back(*pts);
    }
    return r;
}

static void testRecorderPacing() {
    for (double target : {60.0, 30.0}) {
        auto a = pace(0.0, 60.0, target, 10.0);
        auto b = pace(1e6, 60.0, target, 10.0);
        const double iv = 1.0 / target;
        const int expected = (int)(10.0 * target);
        char name[128];
        snprintf(name, sizeof name, "recorder: %g fps target, capture count %d +/-1 (start 0 / 1e6 s)", target, expected);
        check(name, abs(a.captures - expected) <= 1 && abs(b.captures - expected) <= 1);
        snprintf(name, sizeof name, "recorder: %g fps target, PTS deltas = interval +/-1 ms", target);
        check(name, a.minDelta > iv - 0.001 && a.maxDelta < iv + 0.001 &&
                    b.minDelta > iv - 0.001 && b.maxDelta < iv + 0.001);
        snprintf(name, sizeof name, "recorder: %g fps target, PTS strictly increasing", target);
        check(name, a.ptsIncreasing && b.ptsIncreasing);
        bool same = a.pts.size() == b.pts.size();
        for (size_t i = 0; same && i < a.pts.size(); ++i) same = fabs(a.pts[i] - b.pts[i]) < 1e-6;
        snprintf(name, sizeof name, "recorder: %g fps target, start 0 and 1e6 s give the same result", target);
        check(name, same);
    }
    auto hi = pace(1e6, 120.0, 60.0, 10.0);
    checkf("recorder: 120 Hz source -> 60 fps keeps 600 +/-1 frames", abs(hi.captures - 600) <= 1, hi.captures);

    internal::ScreenRecorderPacer p;
    p.start(5000.0, 30.0, 2.0);
    check("recorder: duration cutoff at start + duration",
          !p.reachedDuration(5001.99) && p.reachedDuration(5002.0));
    // A second recording starts its PTS at 0 again (lastPts is reset).
    p.start(100.0, 30.0, 0.0);
    for (int i = 0; i < 90; ++i) p.onFrame(100.0 + i / 30.0);
    p.start(200.0, 30.0, 0.0);
    auto first = p.onFrame(200.0);
    check("recorder: a new recording starts at PTS 0", first && *first < 1e-3);

    // ScreenRecorder never reads a clock itself: it calls the pacer's start()
    // and, once per frame, tick(), both on the pacer's clock, which
    // resetElapsedTimeCounter() doesn't move (#229). Driven here exactly so.
    // Timing-sensitive: the 1 s recording leaves a busy runner ~0.96 s of
    // slack between the reset and the next tick.
    internal::ScreenRecorderPacer live;
    live.start(30.0, 1.0);                       // starts now; 1 s long
    sleepMs(40);
    double c0 = internal::ScreenRecorderPacer::clockNow();
    resetElapsedTimeCounter();
    double c1 = internal::ScreenRecorderPacer::clockNow();
    check("recorder: its clock is not reset by resetElapsedTimeCounter()",
          c1 >= c0 && fabs(c1 - internal::getUptimeSeconds()) < 0.05);
    auto k = live.tick();
    checkf("recorder: a frame after a reset is captured and keeps its PTS (>= 40 ms)",
           k.capture && !k.reachedDuration && k.wallPts >= 0.039 && k.wallPts < 1.0, k.wallPts);
    sleepMs(1000);
    auto end = live.tick();
    checkf("recorder: a reset does not push the duration cutoff back",
           end.reachedDuration && !end.capture, end.wallPts);
}

// ---------------------------------------------------------------------------
// 7b. tc_get_health uptime (the MCP tool, called directly)
// ---------------------------------------------------------------------------
static void testHealthUptime() {
    mcp::registerInspectionTools();
    // Window size without a window: pixel-perfect mode reads sokol's
    // framebuffer size (1 x 1 before init) instead of dividing by its DPI (0).
    const bool pixelPerfect = internal::pixelPerfectMode;
    internal::pixelPerfectMode = true;
    resetElapsedTimeCounter();
    string reply = mcp::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"tc_get_health","arguments":{}}})");
    internal::pixelPerfectMode = pixelPerfect;
    double up = -1.0;
    try {
        auto j = json::parse(reply);
        auto health = json::parse(j["result"]["content"][0]["text"].get<string>());
        up = health["uptimeSec"].get<double>();
    } catch (...) {}
    checkf("tc_get_health: uptimeSec is process uptime, not the reset counter",
           up >= 0.11 && fabs(up - internal::getUptimeSeconds()) < 0.5 && getElapsedTime() < 0.5, up);
}

// ---------------------------------------------------------------------------
// 8. runHeadlessApp loop
// ---------------------------------------------------------------------------
static atomic<int> g_hUpdates{0};
static int    g_hMaxPerPass = 0;
static double g_hPassTime = -1.0;
static int    g_hInPass = 0;
static bool   g_hDtNominal = true;
static atomic<int> g_hMaxQueueLatency{0};
static atomic<int> g_hJobsRun{0};
static atomic<bool> g_hStop{false};

struct SlowHeadlessApp : App {
    thread worker;

    void setup() override {
        // A worker hands work to the main thread; it must be served between
        // bounded passes, not after an ever-growing catch-up loop.
        worker = thread([] {
            while (!g_hStop.load()) {
                int postedAt = g_hUpdates.load();
                runOnMainThread([postedAt] {
                    int lat = g_hUpdates.load() - postedAt;
                    if (lat > g_hMaxQueueLatency.load()) g_hMaxQueueLatency.store(lat);
                    ++g_hJobsRun;
                });
                this_thread::sleep_for(chrono::milliseconds(5));
            }
        });
    }

    void update() override {
        int n = ++g_hUpdates;
        if (getDeltaTime() != 1.0 / 60.0) g_hDtNominal = false;

        // Updates sharing one frame-time sample ran in the same outer pass.
        double pass = getFrameElapsedTime();
        if (pass != g_hPassTime) { g_hPassTime = pass; g_hInPass = 0; }
        g_hMaxPerPass = max(g_hMaxPerPass, ++g_hInPass);

        // 20 ms of work per 16.7 ms step: the loop can never catch up.
        auto until = chrono::steady_clock::now() + chrono::milliseconds(20);
        while (chrono::steady_clock::now() < until) {}
        if (n >= 80) requestExit();
    }

    void exit() override {
        g_hStop.store(true);
        if (worker.joinable()) worker.join();
    }
};

// A light headless app reading getFrameRate() from update(): a loop pass is
// ~1 ms here, far shorter than a 60 Hz step, so whole-step counts over the
// last 10 passes read 0 most of the time.
static vector<double> g_lightRates;
struct LightHeadlessApp : App {
    int n = 0;
    void update() override {
        if (++n > 15) g_lightRates.push_back(getFrameRate());
        if (n >= 75) requestExit();
    }
};

// A fast headless app (1 kHz) whose passes last ~16 ms: the first step of each
// pass sleeps 15 ms. The cap (getMaxUpdateSteps()) applies per pass at any
// rate, like the main loop's per frame (#228), so ~16 steps are due and the
// cap's worth run.
static int g_lpMaxPerPass = 0;
struct LongPassHeadlessApp : App {
    double passTime = -1.0;
    int inPass = 0, passes = 0;
    void update() override {
        double pass = getFrameElapsedTime();   // one sample per loop pass
        if (pass != passTime) {
            passTime = pass;
            inPass = 0;
            if (++passes >= 8) requestExit();
            else sleepMs(15);
        }
        g_lpMaxPerPass = max(g_lpMaxPerPass, ++inPass);
    }
};

// A light fast headless app (1 kHz): the loop's own sleep must not stretch a
// pass past the default 10-step cap. With Sleep() at the default Windows
// timer resolution (~15.6 ms) a pass would span ~16 steps, and the cap would
// hold it to ~640 updates/s. The rate is taken between the first steps of two
// passes, so it has no partial pass at either end.
static int    g_fMaxPerPass = 0;
static double g_fRate = 0.0;
struct FastHeadlessApp : App {
    double passTime = -1.0;
    int updates = 0, inPass = 0, passes = 0, countA = 0;
    Clk::time_point timeA;
    void update() override {
        double pass = getFrameElapsedTime();   // one sample per loop pass
        if (pass != passTime) {
            passTime = pass;
            inPass = 0;
            const auto now = Clk::now();
            if (++passes == 3) {                 // skip the start-up passes
                countA = updates;
                timeA = now;
            } else if (passes > 3) {
                const double s = chrono::duration<double>(now - timeA).count();
                if (s >= 0.5) {
                    g_fRate = (updates - countA) / s;
                    requestExit();
                }
            }
        }
        ++updates;
        g_fMaxPerPass = max(g_fMaxPerPass, ++inPass);
    }
};

// A 100 Hz headless app that stalls once for 150 ms: afterwards about 15
// steps are due. The default cap runs 10 of them in the next pass; with
// setMaxUpdateSteps(0) that pass runs them all.
static int g_sMaxPerPass = 0;
struct StallHeadlessApp : App {
    double passTime = -1.0;
    int inPass = 0, updates = 0;
    void update() override {
        double pass = getFrameElapsedTime();   // one sample per loop pass
        if (pass != passTime) { passTime = pass; inPass = 0; }
        g_sMaxPerPass = max(g_sMaxPerPass, ++inPass);
        if (++updates == 5) sleepMs(150);
        if (updates >= 40) requestExit();
    }
};

// HeadlessSleeper: the waits of 21 sleeps, sorted. The median keeps a busy
// machine's odd late wake-up out of a "returns soon" check, and the minimum
// keeps its odd slow yield out of a "returns at once" check.
static vector<double> sleeperWaits(double seconds) {
    internal::HeadlessSleeper sleeper;
    vector<double> took;
    for (int i = 0; i < 21; ++i) {
        const auto t0 = Clk::now();
        sleeper.sleep(seconds);
        took.push_back(chrono::duration<double>(Clk::now() - t0).count());
    }
    sort(took.begin(), took.end());
    return took;
}

static void testHeadlessLoop() {
    int headlessWarningsBefore = g_headlessDropWarnings;
    runHeadlessApp<SlowHeadlessApp>(HeadlessSettings().setFps(60));
    checkf("headless: dt is the nominal 1/fps", g_hDtNominal, 1.0 / 60.0);
    checkf("headless: at most 10 updates per pass", g_hMaxPerPass <= 10, g_hMaxPerPass);
    checkf("headless: overloaded loop hits the cap (not unbounded)", g_hMaxPerPass == 10, g_hMaxPerPass);
    checkf("headless: dropped time warned exactly once, as the headless loop",
           g_headlessDropWarnings - headlessWarningsBefore == 1, g_headlessDropWarnings - headlessWarningsBefore);
    // Posted during a pass, served at the start of the next one: at most one
    // capped pass (10 updates) + the update in flight when it was posted.
    checkf("headless: runOnMainThread served within one pass", g_hJobsRun.load() > 0 && g_hMaxQueueLatency.load() <= 11,
           g_hMaxQueueLatency.load());

    runHeadlessApp<LightHeadlessApp>(HeadlessSettings().setFps(60));
    int near60 = 0;
    for (double r : g_lightRates) if (fabs(r - 60.0) <= 1.0) ++near60;
    char name[128];
    snprintf(name, sizeof name, "headless: getFrameRate() in update() reads 60 +/-1 (%d of %d)",
             near60, (int)g_lightRates.size());
    check(name, !g_lightRates.empty() && near60 * 10 >= (int)g_lightRates.size() * 9);

    runHeadlessApp<LongPassHeadlessApp>(HeadlessSettings().setFps(1000));
    checkf("headless: 1 kHz, ~16 ms passes: still 10 steps per pass",
           g_lpMaxPerPass == 10, g_lpMaxPerPass);

    // runHeadlessApp takes the same setMaxUpdateSteps() cap as the main loop.
    setMaxUpdateSteps(4);
    g_lpMaxPerPass = 0;
    runHeadlessApp<LongPassHeadlessApp>(HeadlessSettings().setFps(1000));
    checkf("headless: setMaxUpdateSteps(4): 1 kHz, ~16 ms passes run 4 steps per pass",
           g_lpMaxPerPass == 4, g_lpMaxPerPass);
    setMaxUpdateSteps(0);
    runHeadlessApp<StallHeadlessApp>(HeadlessSettings().setFps(100));
    checkf("headless: setMaxUpdateSteps(0): the pass after a 150 ms stall runs every due step (>= 14)",
           g_sMaxPerPass >= 14, g_sMaxPerPass);
    setMaxUpdateSteps(10);

    // Timing-sensitive (real sleeps): medians / minimums of 21 waits, so a
    // busy runner's odd late wake-up doesn't decide the result.
    // Lower bound at half the wait: a timer may fire a few microseconds early
    // against steady_clock, but a sleeper that doesn't sleep would busy-spin.
    const vector<double> shortWaits = sleeperWaits(0.0002);
    const double shortWait = shortWaits[shortWaits.size() / 2];
    checkf("headless sleeper: 0.2 ms wait < 5 ms (not a 15.6 ms tick; median)",
           shortWait >= 0.0001 && shortWait < 0.005, shortWait);
    const double noWait = sleeperWaits(0.0).front();
    checkf("headless sleeper: a 0 wait returns at once (< 1 ms; fastest)", noWait < 0.001, noWait);

    // Timing-sensitive (real time over 0.5 s): the bound sits between the
    // ~1000/s a working sleeper gives and the ~640/s of a 15.6 ms tick, with
    // room for a busy runner's late wake-ups (each one drops what exceeds 10
    // steps).
    runHeadlessApp<FastHeadlessApp>(HeadlessSettings().setFps(1000));
    checkf("headless: 1 kHz keeps up (>= 800/s; 16 ms passes give ~640)",
           g_fRate >= 800.0, g_fRate);
    checkf("headless: ...within the 10-step cap per pass",
           g_fMaxPerPass >= 1 && g_fMaxPerPass <= 10, g_fMaxPerPass);
}

int main() {
    // Must run first: proves the clock origin predates the first read.
    testElapsedClock();

    g_logListener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.find("fell behind") != string::npos) {
            ++g_dropWarnings;
            if (e.message.find("Headless loop") != string::npos) ++g_headlessDropWarnings;
        }
    });

    testFixedStepHelper();
    testMainLoopUpdates();
    testMainLoopDraw();
    testFrameSkip();
    testNodeTimers();
    testNodeTimersInLoop();
    testTimersCountFromCreation();
    testModeSwitchMeasuredDelta();
    testReparentChargesOnce();
    testRecorderPacing();
    testHealthUptime();
    testHeadlessLoop();

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
