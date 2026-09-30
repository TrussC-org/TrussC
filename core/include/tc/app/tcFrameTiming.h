#pragma once

// =============================================================================
// tcFrameTiming.h - fixed-step and frame-skip decisions for the loops (internal)
// =============================================================================
//
// Pure helpers shared by the main loop (_frame_cb in TrussC.h), runHeadlessApp
// (tcHeadlessApp.h) and the per-window throttle (windowThrottleShouldTick in
// tcWindowContext.h). They only transform the numbers they are given, so their
// timing behaviour is testable headless (core/tests/frameTiming). The one
// exception is HeadlessSleeper, the headless loop's OS sleep.
//
// Defined in tcGlobal.cpp (non-inline): the one-time warning keeps its flags
// there, and the hot-reload Host and Guest share them.
// =============================================================================

namespace trussc {
namespace internal {

// The fixed-step loops run at most getMaxUpdateSteps() update steps per frame
// or pass (setMaxUpdateSteps, TrussC.h; default 10, <= 0: no limit, #228).
// When more are pending (after a stall, when update() is slower than its own
// rate, or when the update rate is more than that many times the frame rate)
// the excess time is dropped instead of replayed, and a one-time warning is
// logged.

struct FixedStepAdvance {
    int steps = 0;             // update steps to run now (<= maxSteps)
    double droppedTime = 0.0;  // seconds discarded beyond the cap (0 = none)
};

// Add `elapsed` seconds to a fixed-step accumulator and take out the steps of
// `interval` seconds to run now: at most `maxSteps` (<= 0: no limit). Whatever
// is left beyond the cap is dropped (reported in droppedTime), so the
// accumulator always ends below one interval. A non-positive or non-finite
// interval runs no steps.
FixedStepAdvance advanceFixedStep(double& accumulator, double elapsed,
                                  double interval, int maxSteps);

// Longest sleep between two passes of the headless loop (runHeadlessApp): it
// also drains runOnMainThread work once per pass.
constexpr double headlessMaxSleepTime = 0.001;

// How long the headless loop sleeps after a pass: until its next update step
// is due, at most headlessMaxSleepTime. `accumulator` is the fixed-step
// accumulator after the pass's advanceFixedStep (the time already owed at the
// pass start), `spent` the seconds the pass has taken since then. Returns 0
// when the next step is already due. A non-positive or non-finite interval
// sleeps headlessMaxSleepTime. The headless loop caps catch-up per pass like
// the main loop per frame (getMaxUpdateSteps()), so a pass must stay short at
// a fast rate: sleeping a fixed 1 ms would span 10 steps at 10 kHz.
double headlessSleepTime(double accumulator, double interval, double spent);

// Sleeps between the headless loop's passes. std::this_thread::sleep_for
// (Sleep()) on Windows rounds up to the system timer tick, ~15.6 ms by
// default, which would stretch one pass over 15 steps at 1 kHz and hold it
// to ~640 updates/s under the default 10-step cap. There it waits on a
// high-resolution waitable timer instead (Windows 10 1803+, without raising
// the timer resolution for the whole system; older versions fall back to
// sleep_for).
// Elsewhere it is sleep_for. A wait of 0 or less yields. Create one per loop
// and use it from the loop's thread.
class HeadlessSleeper {
public:
    HeadlessSleeper();
    ~HeadlessSleeper();
    HeadlessSleeper(const HeadlessSleeper&) = delete;
    HeadlessSleeper& operator=(const HeadlessSleeper&) = delete;

    // Sleep `seconds` (clamped to 1 s).
    void sleep(double seconds);

private:
    void* timer_ = nullptr;   // Windows: the waitable timer's HANDLE
};

// Frame-skip decision for a target rate driven by a faster (or equal) tick:
// the fixed-fps draw in _frame_cb and the Window::setFps throttle. Ticks when
// the accumulated time is within half a tick of the interval, so a target
// equal to or just above the display rate runs on every tick (a tick arriving
// a little early no longer fails the test and gets skipped), while integer
// ratios (120 -> 60, 60 -> 30) still run every other tick. After a tick one
// interval is consumed; a large overshoot (a stall) resets the accumulator
// instead of bursting, and it is floored at -interval. Same half-frame
// tolerance as ScreenRecorder's decimation (#142).
bool frameSkipShouldTick(double& accumulator, double elapsed, double interval);

// The fixed-step loops that can drop time.
enum class FixedStepLoop { Main, Headless };

// Log, once per loop and process, that a loop dropped fixed-step time (#228)
// after running `stepsRun` steps (its cap, getMaxUpdateSteps()) in one frame
// or pass.
void warnUpdateStepsDropped(FixedStepLoop loop, double droppedTime, double interval,
                            int stepsRun);

} // namespace internal
} // namespace trussc
