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
// Headless deliberately dispatches only the App virtuals, not the node tree
// (children, mods and timers have a separate lifecycle decision).
inline std::vector<EventListener> registerHeadlessAppEventListeners(
        const std::shared_ptr<std::weak_ptr<App>>& target) {
    std::vector<EventListener> listeners;
    auto warned = std::make_shared<std::atomic<bool>>(false);
    // A worker may retain a notify snapshot through shutdown. It must not
    // retain the App and eventually run its destructor on that worker.
    auto bind = [&](auto& event, auto callback, bool exiting = false) {
        listeners.push_back(FrameworkEventAccess::listen(event,
            [target, callback, warned, exiting] {
                if (!isMainThread()) {
                    if (!warned->exchange(true))
                        logWarning("App") << "App events must be delivered on the main thread";
                    return;
                }
                if (auto owner = target->lock()) {
                    auto lifetime = appOwner(*owner);
                    if (!lifetime || (!exiting && !lifetime->token->load())) return;
                    AppOwnerScope scope(lifetime);
                    callback(*owner);
                }
            }, EventPriority::App));
    };
    bind(nonWindowContext().coreEvents->update, [](App& target) { target.update(); });
    bind(nonWindowContext().coreEvents->exit, [](App& target) { deliverAppExit(target); }, true);
    return listeners;
}

struct HeadlessUpdateState {
    double accumulator = 0.0;
    bool updateStepsDropWarned = false; // Main-thread-only warning episode
};

// One pass, with elapsed time supplied by the runner (or a deterministic
// test). A cancellable request is dispatched only after all owed steps.
inline void runHeadlessUpdatePass(App& app, WindowContext& ctx,
                                  HeadlessUpdateState& state, double elapsed,
                                  double targetDelta) {
    RuntimeActivityScope activity;
    FixedStepAdvance adv = advanceFixedStep(
        state.accumulator, elapsed, targetDelta, getMaxUpdateSteps());
    warnUpdateStepsDropped(FixedStepLoop::Headless, state.updateStepsDropWarned,
                           adv.droppedTime, targetDelta, adv.steps);
    int ran = 0;
    for (; ran < adv.steps; ++ran) {
        headless::pollExitSignal();
        if (!headless::running || app.isExitRequested()) break;
        ctx.updateDeltaTime = targetDelta;
        EntryStackGuard guard(AppEntry::Update);
        // Services such as Box2d guard against stepping twice in one update.
        // Match the windowed loop's counter before delivering this step.
        ++ctx.updateCount;
        ctx.coreEvents->update.notify();
        headless::frameCount++;
    }
    // As in the windowed loop, steps cut short do not count toward the rate.
    recordUpdateRateSample(ctx, elapsed,
        (elapsed - adv.droppedTime) / targetDelta - (adv.steps - ran));

    headless::pollExitSignal();
    if (headless::running && !app.isExitRequested()
            && headless::quitRequested.exchange(false)) {
        if (dispatchExitRequested()) headless::running = false;
        headless::pollExitSignal();
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
    internal::beginRuntimeRun();
    internal::beginFrameworkLifetime();
    struct EndFrameworkLifetime {
        ~EndFrameworkLifetime() {
            if (auto token = internal::appRunToken()) token->store(false);
            headless::active = false;
            internal::endFrameworkLifetime();
        }
    } endFrameworkLifetime;

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

    struct StopHeadlessServices {
        ~StopHeadlessServices() {
            internal::shutdownRuntime();
            internal::TeardownSteps steps;
            steps.run("headless resource cancellation", [] { internal::cancelResourceBackends(); });
#ifndef __EMSCRIPTEN__
            steps.run("headless MCP stop", [] { mcp::stopHttpServer(); });
#endif
            steps.run("headless console stop", [] { console::stop(); });
            steps.run("headless resource completion", [] { internal::waitForResourceLoadsAtAppExit(); });
        }
    } stopServices;

    // Record the main thread id (this runner owns the app/update loop), so
    // isMainThread() / runOnMainThread() behave the same as in the windowed app.
    getMainThreadId();

    // Reset state
    headless::active = true;
    headless::frameCount = 0;

    // Headless callbacks use the non-window context without registering a Window.
    auto& ctx = internal::nonWindowContext();
    auto* previousContext = internal::currentWindowCtx();
    internal::currentWindowCtx() = &ctx;
    struct RestoreContext {
        internal::WindowContext* previous;
        ~RestoreContext() { internal::currentWindowCtx() = previous; }
    } restoreContext{previousContext};
    ctx.drawCount = 0;
    ctx.updateCount = 0;
    ctx.resourceTickCount = 0;
    internal::sampleFrameTime(ctx);

    // Construct the virtual attachment with the same owner token as a Window
    // attachment. Its shared ownership initializes weak_from_this(); headless
    // execution still skips scene-tree update, draw and cleanup.
    auto appSlot = std::make_shared<std::weak_ptr<App>>();
    auto appListeners = internal::registerHeadlessAppEventListeners(appSlot);
    std::shared_ptr<AppClass> app;
    app = internal::makeConstructedAppAt<AppClass>(&ctx);
    internal::recordHeadlessAttachment(app, ctx);
    struct EndAttachment {
        ~EndAttachment() { internal::shutdownRuntime(); }
    } endAttachment;
    ctx.rootNode = app;
    *appSlot = app;
    internal::appOwner(*app)->notifyWindowExit = true;

    // setup() once, then the framework's post-setup hook, as the windowed
    // App gets them on its first tree update: the App's audioOut() /
    // audioIn() are subscribed only once setup() has returned (#426).
    {
        internal::RuntimeActivityScope activity;
        internal::AppOwnerScope owner(internal::appOwner(*app));
        internal::setupNodeOnce(*app);
    }

    // Main loop: fixed timestep at the nominal 1/fps (getDeltaTime() reports
    // exactly that), at most getMaxUpdateSteps() steps per pass (the main
    // loop's cap per frame, setMaxUpdateSteps; default 10). After a stall
    // (sleep/resume) or when update() is slower than its rate, the excess
    // time is dropped with one warning per stall instead of replayed, and
    // runOnMainThread work is drained between bounded passes (#228). Between
    // passes the loop sleeps until the next step is due, at most 1 ms, on a
    // precise timer (HeadlessSleeper), so a fast rate stays well under the
    // cap per pass.
    const double targetDelta = 1.0 / headless::targetFps;
    internal::HeadlessUpdateState updateState;
    auto lastTime = std::chrono::steady_clock::now();
    internal::HeadlessSleeper sleeper;

    while (headless::running && !app->isExitRequested()) {
        headless::pollExitSignal();
        if (!headless::running) break;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        internal::sampleFrameTime(ctx);
        {
            internal::HostTurnScope turn;
            internal::runHeadlessUpdatePass(*app, ctx, updateState, elapsed, targetDelta);
        }
        if (!headless::running || app->isExitRequested()) break;

        // Sleep until the next step is due (at most 1 ms), counted from the
        // pass start: the steps above already took part of that time.
        const double spent = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - now).count();
        sleeper.sleep(internal::headlessSleepTime(updateState.accumulator, targetDelta, spent));
    }

    internal::shutdownRuntime();
    internal::cancelResourceBackends();
#ifndef __EMSCRIPTEN__
    mcp::stopHttpServer();
#endif
    console::stop();

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__EMSCRIPTEN__)
    internal::shutdownKeepScreenOn();
#endif
    internal::waitForResourceLoadsAtAppExit();
    appListeners.clear();
    ctx.rootNode.reset();   // no longer the running App

    // Headless apps leave the audio device running (no shutdownAudio() on
    // this path), so log the drops the rate limit still holds back here.
    internal::flushAudioDiagnostics();

    headless::active = false;
    return internal::appExitCode();
}

} // namespace trussc
