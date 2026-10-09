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
#else
#include <unistd.h>
#endif

namespace trussc {

// tcAudio_impl.cpp: log the dropped plays that were only counted (see
// tcSound.h); the flush variant ignores the rate limit.
namespace internal {
void pumpAudioDiagnostics();
void flushAudioDiagnostics();

#ifdef _WIN32
// The code pages HeadlessConsoleUtf8 will restore, for the console control
// handler's forced exit (headless::consoleHandler); 0 for each unset page.
// Headless only, where hot reload never runs, so a per-module copy is fine
// (tools/header_state_allowlist.txt).
inline ConsoleCodePages headlessRestoreConsoleCP;

// runHeadlessApp()'s console input and output code pages: UTF-8 for the guard's
// lifetime, as the windowed app gets from sapp_desc.win32.console_utf8.
// runHeadlessApp() declares it before the app, so each successfully set code
// page comes back after the app's destructor, and when an exception that
// the caller catches leaves the function. An uncaught exception ends the
// process without unwinding, and does not restore them. Without a console
// both sets fail and nothing is restored.
struct HeadlessConsoleUtf8 {
    HeadlessConsoleUtf8()
        : originalOutput(GetConsoleOutputCP()), originalInput(GetConsoleCP()),
          outputSet(SetConsoleOutputCP(CP_UTF8) != 0),
          inputSet(SetConsoleCP(CP_UTF8) != 0) {
        headlessRestoreConsoleCP.output = outputSet ? originalOutput : 0;
        headlessRestoreConsoleCP.input = inputSet ? originalInput : 0;
    }
    ~HeadlessConsoleUtf8() {
        if (outputSet) SetConsoleOutputCP(originalOutput);
        if (inputSet) SetConsoleCP(originalInput);
        headlessRestoreConsoleCP.output = 0;
        headlessRestoreConsoleCP.input = 0;
    }
    HeadlessConsoleUtf8(const HeadlessConsoleUtf8&) = delete;
    HeadlessConsoleUtf8& operator=(const HeadlessConsoleUtf8&) = delete;

    UINT originalOutput;
    UINT originalInput;
    bool outputSet;
    bool inputSet;
};
#endif
}

// ---------------------------------------------------------------------------
// Headless mode internal state (extends tcHeadlessState.h)
// ---------------------------------------------------------------------------
namespace headless {
    // Headless-only state: hot reload is windowed and never runs this loop, so
    // a per-module copy is fine (tools/header_state_allowlist.txt).

    // First signal only publishes; a second pending signal forces exit.
    // The main thread acknowledges delivery and records the reason.
    static_assert(std::atomic<sig_atomic_t>::is_always_lock_free);
    inline std::atomic<sig_atomic_t> pendingSignal{0};
    inline sig_atomic_t deliveredSignal = 0;

    // Target FPS for headless mode (default: 60)
    inline float targetFps = 60.0f;

    // Frame count
    inline uint64_t frameCount = 0;

#ifdef _WIN32
    // A second Ctrl+C/Ctrl+Break must still end a hung app, including while
    // listeners or cleanup run: restore both code pages and let the default
    // console handler call ExitProcess.
    inline BOOL WINAPI consoleHandler(DWORD signal) {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
            if (!pendingSignal.exchange(SIGINT, std::memory_order_relaxed)) return TRUE;
            const UINT output = internal::headlessRestoreConsoleCP.output.load();
            const UINT input = internal::headlessRestoreConsoleCP.input.load();
            if (output != 0) SetConsoleOutputCP(output);
            if (input != 0) SetConsoleCP(input);
            return FALSE;
        }
        if (signal == CTRL_CLOSE_EVENT) {
            pendingSignal.store(SIGTERM, std::memory_order_relaxed);
            return TRUE;
        }
        return FALSE;
    }
#else
    // POSIX signal handler
    inline void signalHandler(int sig) {
        if (pendingSignal.exchange(sig, std::memory_order_relaxed)) _exit(128 + sig);
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

    // Main thread only, including between catch-up steps.
    inline void pollExitSignal() {
        // Keep the pending value so a later signal escalates even after
        // delivery. Signals are uncancellable; only a new run resets it.
        const sig_atomic_t sig = pendingSignal.load(std::memory_order_relaxed);
        if (sig == deliveredSignal) return;
        deliveredSignal = sig;
        if (sig) {
            internal::setExitReason(sig == SIGTERM ? "sigterm" : "sigint");
            running = false;
        }
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

namespace internal {
// One pass, with elapsed time supplied by the runner (or a deterministic
// test). A cancellable request is dispatched only after all owed steps.
inline void runHeadlessUpdatePass(App& app, WindowContext& ctx,
                                  double& accumulator, double elapsed,
                                  double targetDelta) {
    FixedStepAdvance adv = advanceFixedStep(
        accumulator, elapsed, targetDelta, getMaxUpdateSteps());
    if (adv.droppedTime > 0.0) {
        warnUpdateStepsDropped(FixedStepLoop::Headless,
                               adv.droppedTime, targetDelta, adv.steps);
    }
    int ran = 0;
    for (; ran < adv.steps; ++ran) {
        headless::pollExitSignal();
        if (!headless::running || app.isExitRequested()) break;
        ctx.updateDeltaTime = targetDelta;
        EntryStackGuard guard(AppEntry::Update);
        crashFrame(headless::frameCount);
        CrashPhaseScope updatePhase("update");
        app.update();
        headless::frameCount++;
    }
    // As in the windowed loop, steps cut short do not count toward the rate.
    recordUpdateRateSample(ctx, elapsed,
        (elapsed - adv.droppedTime) / targetDelta - (adv.steps - ran));

    headless::pollExitSignal();
    if (headless::running && !app.isExitRequested()
            && headless::quitRequested.exchange(false)) {
        EntryStackGuard guard(AppEntry::Event);
        setExitBlockReason("");
        ExitRequestEventArgs args;
        events().exitRequested.notify(args);
        headless::pollExitSignal();
        if (args.cancel) {
            // A listener may also issue an uncancellable exit.
            if (headless::running) {
                setExitBlockReason(args.reason);
                clearExitReason();
            }
        } else {
            headless::running = false;
        }
    }
}
} // namespace internal

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
    internal::installCrashHandler();
    internal::CrashPhaseScope crashPhase("headless");

    // Set target FPS
    headless::targetFps = settings.targetFps;

    // Reset before installing handlers: keep any signal received during
    // installation, and start consecutive runs with no stale exit state.
    headless::running = true;
    headless::quitRequested = false;
    headless::pendingSignal = 0;
    headless::deliveredSignal = 0;
    internal::appExitCode() = 0;
    internal::clearExitReason();
    internal::setExitBlockReason("");

    // Install signal handlers
    headless::installSignalHandlers();

#ifdef _WIN32
    // Console input and output code pages UTF-8 until the app is destroyed (see
    // internal::HeadlessConsoleUtf8)
    internal::HeadlessConsoleUtf8 consoleUtf8;
#endif

    // Record the main thread id (this runner owns the app/update loop), so
    // isMainThread() / runOnMainThread() behave the same as in the windowed app.
    getMainThreadId();

    // Reset state
    headless::active = true;
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
        headless::pollExitSignal();
        if (!headless::running) break;
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

        internal::runHeadlessUpdatePass(*app, ctx, accumulator, elapsed, targetDelta);
        if (!headless::running || app->isExitRequested()) break;

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
        // The device keeps running; finish any callback before cleanup().
        internal::detachAppAudio(*app);
        app->cleanup();
    }

    ctx.rootNode.reset();   // no longer the running App

    // Headless apps leave the audio device running (no shutdownAudio() on
    // this path), so log the drops the rate limit still holds back here.
    internal::flushAudioDiagnostics();

    headless::active = false;
    return internal::appExitCode();
}

} // namespace trussc
