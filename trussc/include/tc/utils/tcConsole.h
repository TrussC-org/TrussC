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
inline ConsoleEventArgs parseLine(const std::string& line) {
    ConsoleEventArgs args;
    args.raw = line;

    std::istringstream iss(line);
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
    if (detail::isRunning().load()) {
        return; // Already running
    }

    detail::isRunning().store(true);
    detail::getThread() = std::make_unique<std::thread>(detail::readThread);
}

/// Stop console input thread
/// Call in setup() to disable
inline void stop() {
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
}

/// Process commands in queue
/// Called every frame within _frame_cb() in TrussC.h
/// Fires events to events().console if commands exist
inline void processQueue() {
    if (!detail::isRunning().load()) {
        return;
    }

    ConsoleEventArgs args;
    while (detail::getChannel().tryReceive(args)) {
        events().console.notify(args);
    }
}

/// Whether console is enabled
inline bool isEnabled() {
    return detail::isRunning().load();
}

} // namespace console
} // namespace trussc
