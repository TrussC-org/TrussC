#pragma once

// =============================================================================
// TrussC Headless Mode Runner
// Run TrussC apps without window/graphics context
// =============================================================================

#include "tcHeadlessState.h"
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

    // Start time
    inline std::chrono::high_resolution_clock::time_point startTime;

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

    // Get elapsed time since start
    inline double getElapsedTime() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - startTime).count();
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
    headless::startTime = std::chrono::high_resolution_clock::now();

    // Create app instance
    AppClass app;

    // Call setup
    app.setup();

    // Main loop
    const double targetDelta = 1.0 / headless::targetFps;
    double accumulator = 0.0;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (headless::running && !app.isExitRequested()) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        accumulator += elapsed;

        // Run work marshalled from worker threads (runOnMainThread, Event
        // Deliver::Main) on the main thread, mirroring the windowed _frame_cb.
        internal::drainMainThreadQueue();
        internal::pumpAudioDiagnostics();

        // Fixed timestep update
        while (accumulator >= targetDelta) {
            app.update();
            headless::frameCount++;
            accumulator -= targetDelta;
        }

        // Sleep to reduce CPU usage (1ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
