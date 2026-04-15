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
#include <memory>
#include "tcThreadChannel.h"
#include "../events/tcEventArgs.h"
#include "../events/tcCoreEvents.h"

namespace trussc {
namespace console {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace detail {

inline ThreadChannel<ConsoleEventArgs>& getChannel() {
    static ThreadChannel<ConsoleEventArgs> channel;
    return channel;
}

inline std::atomic<bool>& isRunning() {
    static std::atomic<bool> running{false};
    return running;
}

inline std::unique_ptr<std::thread>& getThread() {
    static std::unique_ptr<std::thread> t;
    return t;
}

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

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Start console input thread
/// Called automatically within runApp() in TrussC.h
inline void start() {
#ifdef __EMSCRIPTEN__
    // Console input is not available on web (no stdin/threads)
    std::cerr << "[TrussC] console::start() is not available on web platform" << std::endl;
    return;
#else
    if (detail::isRunning().load()) {
        return; // Already running
    }

    detail::isRunning().store(true);
    detail::getThread() = std::make_unique<std::thread>(detail::readThread);
#endif
}

/// Stop console input thread
/// Call in setup() to disable
inline void stop() {
#ifdef __EMSCRIPTEN__
    // No-op on web
    return;
#else
    if (!detail::isRunning().load()) {
        return; // Already stopped
    }

    detail::isRunning().store(false);
    detail::getChannel().close();

    // If thread is waiting on getline, it may block indefinitely
    // unless stdin is closed, so detach it
    if (detail::getThread() && detail::getThread()->joinable()) {
        detail::getThread()->detach();
    }
    detail::getThread().reset();
#endif
}

/// Process commands in queue
/// Called every frame within _frame_cb() in TrussC.h
/// Fires events to events().console if commands exist
inline void processQueue() {
#ifdef __EMSCRIPTEN__
    // No-op on web
    return;
#else
    if (!detail::isRunning().load()) {
        return;
    }

    ConsoleEventArgs args;
    while (detail::getChannel().tryReceive(args)) {
        events().console.notify(args);
    }
#endif
}

/// Whether console is enabled
inline bool isEnabled() {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return detail::isRunning().load();
#endif
}

} // namespace console
} // namespace trussc
