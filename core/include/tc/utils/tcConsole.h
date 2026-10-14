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
//   console::start();  // Opt in to stdin commands
//   events().console.listen([](const ConsoleEventArgs& e) {
//       if (e.args[0] == "spawn") {
//           spawnEnemy(stoi(e.args[1]), stoi(e.args[2]));
//       }
//   });
//
//   // Sender side (external process)
//   echo "spawn 100 200" | ./myapp
//
// Opt-in: call console::start() in setup() in windowed or headless apps.
// One detached reader owns stdin after the first start(), until EOF or process
// exit. start()/stop() enable/disable delivery; lines read while stopped are dropped.
//
// =============================================================================

#include <thread>
#include <atomic>
#include <istream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif
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

// Shared state, defined in tcGlobal.cpp, including across hot-reload guests
// (#249). Apps opt in; both runners drain the channel on the main thread.
ThreadChannel<ConsoleEventArgs>& getChannel();
std::atomic<bool>& isRunning();
std::atomic<bool>& spawned();

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

// Read stdin without holding a C stdio lock across a blocking read. Reading
// std::cin (synchronized with stdin by default) can hold that lock forever,
// preventing normal process exit when libc flushes its streams. This buffer
// belongs to the detached reader, not to the process's static stream objects.
class StdinBuffer : public std::streambuf {
protected:
    int_type underflow() override {
#ifdef _WIN32
        DWORD count = 0;
        const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
        if (!ReadFile(handle, buffer_, sizeof(buffer_), &count, nullptr) || count == 0)
            return traits_type::eof();
#else
        ssize_t count;
        do { count = ::read(STDIN_FILENO, buffer_, sizeof(buffer_)); }
        while (count < 0 && errno == EINTR);
        if (count <= 0) return traits_type::eof();
#endif
        setg(buffer_, buffer_, buffer_ + count);
        return traits_type::to_int_type(*gptr());
    }
private:
    char buffer_[4096];
};

// Consume until EOF even while delivery is stopped. Kept separate from the
// raw stream so the delivery boundaries can also be tested deterministically.
inline void readLines(std::istream& input) {
    std::string line;
    while (std::getline(input, line)) {
        if (!isRunning().load()) continue;
        // Raw reads bypass text-mode CRLF translation on every platform.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto args = parseLine(line);
        if (args.args.empty()) continue;
        getChannel().send(std::move(args));
    }
}

inline void readThread() {
    StdinBuffer buffer;
    std::istream input(&buffer);
    readLines(input);
}

} // namespace internal

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Enable stdin command delivery (opt-in; call in setup()).
/// The first call starts the process's only reader; later calls reuse it.
inline void start() {
#ifdef __EMSCRIPTEN__
    // Console input is not available on web (no stdin/threads)
    logNotice("Console") << "console::start() is not available on web platform";
    return;
#else
    if (internal::isRunning().load()) return;

    // Discard anything queued after stop() drained, before enabling delivery.
    internal::getChannel().receiveAll();

    internal::isRunning().store(true);
    // getline may block indefinitely; detach now so std::exit() cannot
    // destroy a joinable thread even if stop() was never called.
    if (!internal::spawned().exchange(true)) {
        std::thread(internal::readThread).detach();
    }
#endif
}

/// Disable delivery and discard pending commands. The reader keeps consuming
/// stdin until EOF; start() can enable delivery again without another thread.
inline void stop() {
#ifdef __EMSCRIPTEN__
    // No-op on web
    return;
#else
    internal::isRunning().store(false);
    internal::getChannel().receiveAll();
#endif
}

namespace internal {

/// Process commands in queue
/// Called each loop by the windowed and headless runners
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
