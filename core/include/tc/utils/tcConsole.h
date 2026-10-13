#pragma once

// =============================================================================
// tcConsole - Command input from stdin
// =============================================================================
//
// Module for receiving commands from external processes like AI assistants.
// Parses lines received from stdin and dispatches them as ConsoleEventArgs.
//
// Usage:
//   // Receiver side (e.g., in tcApp::setup())
//   tcEvents().console.listen([](const ConsoleEventArgs& e) {
//       if (e.args[0] == "spawn") {
//           spawnEnemy(stoi(e.args[1]), stoi(e.args[2]));
//       }
//   });
//
//   // Sender side (external process)
//   echo "spawn 100 200" | ./myapp
//
// Enabled by default. Call console::stop() in setup() to disable.
//
// =============================================================================

#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include "tcThreadChannel.h"
#include "tcLog.h"
#include "../events/tcEventArgs.h"
#include "../events/tcCoreEvents.h"

namespace trussc {
namespace console {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace internal {

// One stdin reader per process, defined in tcGlobal.cpp. The host starts it and
// drains the channel every frame; console::stop() / isEnabled() are typically
// called from setup(), which is guest code under hot reload. Header-inline, a
// Windows guest had its own (never started) copy, so stop() was a no-op (#249).
ThreadChannel<ConsoleEventArgs>& getChannel();
std::atomic<bool>& isRunning();

// Parse line by whitespace and create ConsoleEventArgs
// Comments: everything after '#' is ignored
inline ConsoleEventArgs parseLine(const std::string& line) {
    ConsoleEventArgs args;
    args.raw = line;

    // Strip comments (everything after '#')
    std::string stripped = line;
    size_t commentPos = stripped.find('#');
    if (commentPos != std::string::npos) {
        stripped = stripped.substr(0, commentPos);
    }

    std::istringstream iss(stripped);
    std::string token;
    while (iss >> token) {
        args.args.push_back(token);
    }
    return args;
}

// stdin reading thread
inline void readThread() {
    std::string line;
    while (isRunning().load() && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto args = parseLine(line);
        getChannel().send(std::move(args));
    }
}

} // namespace internal

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Start console input thread
/// Called automatically within runApp() in TrussC.h
inline void start() {
#ifdef __EMSCRIPTEN__
    // Console input is not available on web (no stdin/threads)
    logWarning("Console") << "console::start() is not available on web platform";
    return;
#else
    if (internal::isRunning().load()) {
        return; // Already running
    }

    internal::isRunning().store(true);
    // getline may block indefinitely; detach now so std::exit() cannot
    // destroy a joinable thread even if stop() was never called.
    std::thread(internal::readThread).detach();
#endif
}

/// Stop console input thread
/// Call in setup() to disable
inline void stop() {
#ifdef __EMSCRIPTEN__
    // No-op on web
    return;
#else
    if (!internal::isRunning().load()) {
        return; // Already stopped
    }

    internal::isRunning().store(false);
    internal::getChannel().close();
#endif
}

namespace internal {

/// Process commands in queue
/// Called every frame within _frame_cb() in TrussC.h
/// Fires events to events().console if commands exist
inline void processQueue() {
#ifdef __EMSCRIPTEN__
    // No-op on web
    return;
#else
    if (!internal::isRunning().load()) {
        return;
    }

    ConsoleEventArgs args;
    while (internal::getChannel().tryReceive(args)) {
        events().console.notify(args);
    }
#endif
}

} // namespace internal

/// Whether console is enabled
inline bool isEnabled() {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return internal::isRunning().load();
#endif
}

} // namespace console
} // namespace trussc
