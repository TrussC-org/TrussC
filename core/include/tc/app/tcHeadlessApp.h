#pragma once

// =============================================================================
// TrussC Headless Mode Runner
// Run TrussC apps without window/graphics context
// =============================================================================

#include "tcHeadlessState.h"

#include <chrono>
#include <thread>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#endif

namespace trussc {

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

    // Start time
    inline std::chrono::high_resolution_clock::time_point startTime;

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

    headless::active = false;
    return 0;
}

} // namespace trussc
