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

#ifdef _WIN32
// The code page HeadlessConsoleUtf8 will restore, for the console control
// handler's forced exit (headless::consoleHandler); 0 when there is none.
// Headless only, where hot reload never runs, so a per-module copy is fine
// (tools/header_state_allowlist.txt).
inline std::atomic<UINT> headlessRestoreConsoleCP{0};

// runHeadlessApp()'s console output code page: UTF-8 for the guard's
// lifetime, as the windowed app gets from sapp_desc.win32.console_utf8 (log
// text is UTF-8). runHeadlessApp() declares it before the app, so the code
// page comes back after the app's destructor, and when an exception that
// the caller catches leaves the function. An uncaught exception ends the
// process without unwinding, and does not restore it. Without a console
// the set fails and nothing is restored.
struct HeadlessConsoleUtf8 {
    HeadlessConsoleUtf8()
        : original(GetConsoleOutputCP()), set(SetConsoleOutputCP(CP_UTF8) != 0) {
        if (set) headlessRestoreConsoleCP = original;
    }
    ~HeadlessConsoleUtf8() {
        if (!set) return;
        headlessRestoreConsoleCP = 0;
        SetConsoleOutputCP(original);
    }
    HeadlessConsoleUtf8(const HeadlessConsoleUtf8&) = delete;
    HeadlessConsoleUtf8& operator=(const HeadlessConsoleUtf8&) = delete;

    UINT original;
    bool set;
};
#endif
}

// ---------------------------------------------------------------------------
// Headless mode internal state (extends tcHeadlessState.h)
// ---------------------------------------------------------------------------
namespace headless {
    // Headless-only state: hot reload is windowed and never runs this loop, so
    // a per-module copy is fine (tools/header_state_allowlist.txt).

    // Running flag (set to false by signal handler)
    inline std::atomic<bool> running{true};

    // Target FPS for headless mode (default: 60)
    inline float targetFps = 60.0f;

    // Frame count
    inline uint64_t frameCount = 0;

#ifdef _WIN32
    // Windows console control handler. The first Ctrl+C or Ctrl+Break stops
    // the loop, so the app is destroyed and the console code page restored
    // (internal::HeadlessConsoleUtf8). A second one, while the loop is
    // already stopping, means the app is stuck where the loop flag is not
    // read (setup(), a long update()): restore the code page and fall
    // through to the default handler (ExitProcess), so the keyboard can
    // still end a hung app.
    inline BOOL WINAPI consoleHandler(DWORD signal) {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
            if (running.exchange(false)) return TRUE;
            const UINT cp = internal::headlessRestoreConsoleCP.load();
            if (cp != 0) SetConsoleOutputCP(cp);
            return FALSE;
        }
        if (signal == CTRL_CLOSE_EVENT) {
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

#ifdef _WIN32
    // Console output code page UTF-8 until the app is destroyed (see
    // internal::HeadlessConsoleUtf8)
    internal::HeadlessConsoleUtf8 consoleUtf8;
#endif

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

    // Create app instance. Owned by a shared_ptr, as runApp() owns the
    // windowed App: it is the main window's scene-graph root (getRootNode(),
    // held weakly) while it runs, and weak_from_this() works, so setup() can
    // addChild().
    auto app = std::make_shared<AppClass>();
    ctx.rootNode = app;

    // setup() once, then the framework's post-setup hook, as the windowed
    // App gets them on its first tree update: the App's audioOut() /
    // audioIn() are subscribed only once setup() has returned (#426).
    internal::setupNodeOnce(*app);

    // Main loop: fixed timestep at the nominal 1/fps (getDeltaTime() reports
    // exactly that), at most getMaxUpdateSteps() steps per pass (the main
    // loop's cap per frame, setMaxUpdateSteps; default 10). After a stall
    // (sleep/resume) or when update() is slower than its rate, the excess
    // time is dropped with a one-time warning instead of replayed, and
    // runOnMainThread work is drained between bounded passes (#228). Between
    // passes the loop sleeps until the next step is due, at most 1 ms, on a
    // precise timer (HeadlessSleeper), so a fast rate stays well under the
    // cap per pass.
    const double targetDelta = 1.0 / headless::targetFps;
    double accumulator = 0.0;
    auto lastTime = std::chrono::steady_clock::now();
    internal::HeadlessSleeper sleeper;

    while (headless::running && !app->isExitRequested()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        internal::sampleFrameTime(ctx);

        // Run work marshalled from worker threads (runOnMainThread, Event
        // Deliver::Main) on the main thread, mirroring the windowed _frame_cb.
        // Each app-code call here is an entry point, as in the windowed loop
        // (#349): the stacks go back to their depth before it.
        {
            internal::EntryStackGuard guard(internal::AppEntry::Prelude);
            internal::drainMainThreadQueue();
            internal::pumpAudioDiagnostics();
        }

        // Fixed timestep update
        internal::FixedStepAdvance adv = internal::advanceFixedStep(
            accumulator, elapsed, targetDelta, getMaxUpdateSteps());
        if (adv.droppedTime > 0.0) {
            internal::warnUpdateStepsDropped(internal::FixedStepLoop::Headless,
                                             adv.droppedTime, targetDelta, adv.steps);
        }
        for (int i = 0; i < adv.steps; ++i) {
            ctx.updateDeltaTime = targetDelta;
            internal::EntryStackGuard guard(internal::AppEntry::Update);
            app->update();
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
    {
        internal::EntryStackGuard guard(internal::AppEntry::Exit);
        app->exit();
        app->cleanup();
    }

    // The audio device keeps running: detach the App's audio hooks and wait
    // for a callback in flight before the App goes out of scope (#256).
    internal::detachAppAudio(*app);
    ctx.rootNode.reset();   // no longer the running App

    // Headless apps leave the audio device running (no shutdownAudio() on
    // this path), so log the drops the rate limit still holds back here.
    internal::flushAudioDiagnostics();

    headless::active = false;
    return 0;
}

} // namespace trussc
