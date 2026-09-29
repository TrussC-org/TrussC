#pragma once

// =============================================================================
// TrussC Headless Mode Runner
// Run TrussC apps without window/graphics context
// =============================================================================

#include "tcHeadlessState.h"
#include "tcFrameTiming.h"            // advanceFixedStep / HeadlessSleeper
#include "../utils/tcMainThread.h"   // getMainThreadId / drainMainThreadQueue

#include <chrono>
#include <thread>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#endif

namespace trussc {

// tcAudio_impl.cpp: log the dropped plays that were only counted (see
// tcSound.h); the flush variant ignores the rate limit.
namespace internal {
void pumpAudioDiagnostics();
void flushAudioDiagnostics();
}

// ---------------------------------------------------------------------------
// Headless mode internal state (extends tcHeadlessState.h)
// ---------------------------------------------------------------------------
namespace headless {
    // Running flag (set to false by signal handler)
    inline std::atomic<bool> running{true};

    // Target FPS for headless mode (default: 60)
    inline float targetFps = 60.0f;

    // Frame count
    inline uint64_t frameCount = 0;

#ifdef _WIN32
    // Windows console control handler
    inline BOOL WINAPI consoleHandler(DWORD signal) {
        if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
            running = false;
            return TRUE;
        }
        return FALSE;
    }
#else
    // POSIX signal handler
    inline void signalHandler(int sig) {
        (void)sig;
        running = false;
    }
#endif

    // Install signal handlers
    inline void installSignalHandlers() {
#ifdef _WIN32
        SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
#endif
    }

    // Elapsed time: the same clock as trussc::getElapsedTime() (one steady
    // clock with its origin at program start, #229).
    inline double getElapsedTime() {
        return trussc::getElapsedTime();
    }

    // Get frame count
    inline uint64_t getFrameCount() {
        return frameCount;
    }
}

// ---------------------------------------------------------------------------
// Headless settings
// ---------------------------------------------------------------------------
struct HeadlessSettings {
    float targetFps = 60.0f;  // Target update rate

    HeadlessSettings& setFps(float fps) {
        targetFps = fps;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// Run app in headless mode
// ---------------------------------------------------------------------------
template<typename AppClass>
int runHeadlessApp(const HeadlessSettings& settings = HeadlessSettings()) {
    // Set target FPS
    headless::targetFps = settings.targetFps;

    // Install signal handlers
    headless::installSignalHandlers();

    // Record the main thread id (this runner owns the app/update loop), so
    // isMainThread() / runOnMainThread() behave the same as in the windowed app.
    getMainThreadId();

    // Reset state
    headless::active = true;
    headless::running = true;
    headless::frameCount = 0;

    // Headless apps run in the main window's (GPU-less) context: that is
    // where getDeltaTime() / getFrameRate() / getFrameElapsedTime() read.
    auto& ctx = internal::mainWindowContext();
    internal::sampleFrameTime(ctx);

    // Create app instance
    AppClass app;

    // Call setup
    app.setup();

    // Main loop: fixed timestep at the nominal 1/fps (getDeltaTime() reports
    // exactly that), at most 10 steps per pass (maxUpdateStepsPerFrame, the
    // main loop's cap per frame). After a stall (sleep/resume) or when
    // update() is slower than its rate, the excess time is dropped with a
    // one-time warning instead of replayed, and runOnMainThread work is
    // drained between bounded passes (#228). Between passes the loop sleeps
    // until the next step is due, at most 1 ms, on a precise timer
    // (HeadlessSleeper), so a fast rate stays well under 10 steps per pass.
    const double targetDelta = 1.0 / headless::targetFps;
    double accumulator = 0.0;
    auto lastTime = std::chrono::steady_clock::now();
    internal::HeadlessSleeper sleeper;

    while (headless::running && !app.isExitRequested()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        internal::sampleFrameTime(ctx);

        // Run work marshalled from worker threads (runOnMainThread, Event
        // Deliver::Main) on the main thread, mirroring the windowed _frame_cb.
        internal::drainMainThreadQueue();
        internal::pumpAudioDiagnostics();

        // Fixed timestep update
        internal::FixedStepAdvance adv =
            internal::advanceFixedStep(accumulator, elapsed, targetDelta);
        if (adv.droppedTime > 0.0) {
            internal::warnUpdateStepsDropped(internal::FixedStepLoop::Headless,
                                             adv.droppedTime, targetDelta, adv.steps);
        }
        for (int i = 0; i < adv.steps; ++i) {
            ctx.updateDeltaTime = targetDelta;
            app.update();
            headless::frameCount++;
        }
        // Measured rate: the time the steps consumed, in (fractional) steps,
        // over the wall time. A pass is often shorter than a step (~1 ms on
        // Linux/macOS), so whole-step counts would read 0 in most windows.
        internal::recordUpdateRateSample(ctx, elapsed,
                                         (elapsed - adv.droppedTime) / targetDelta);

        // Sleep until the next step is due (at most 1 ms), counted from the
        // pass start: the steps above already took part of that time.
        const double spent = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - now).count();
        sleeper.sleep(internal::headlessSleepTime(accumulator, targetDelta, spent));
    }

    // Call exit and cleanup
    app.exit();
    app.cleanup();

    // Headless apps leave the audio device running (no shutdownAudio() on
    // this path), so log the drops the rate limit still holds back here.
    internal::flushAudioDiagnostics();

    headless::active = false;
    return 0;
}

} // namespace trussc
