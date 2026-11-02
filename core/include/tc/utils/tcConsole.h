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
//   runtime().events().console.listen([](const ConsoleEventArgs& e) {
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
#include "internal/tcBoundedInput.h"
#include <mutex>
#include <vector>
#include <cstdint>
#include <limits>
#include "tcLog.h"
#include "../events/tcEventArgs.h"
#include "../events/tcCoreEvents.h"

namespace trussc {
namespace internal { void dispatchConsoleEvent(ConsoleEventArgs&); }
namespace console {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace internal {

// Shared state, defined in tcGlobal.cpp, including across hot-reload guests
// (#249). Apps opt in; both runners drain the channel on the main thread.
class CommandChannel;
CommandChannel& getChannel();
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

// This channel owns raw lines, so a queue of whitespace-heavy commands does
// not retain a token-vector expansion. Parsing happens only on consumption.
class CommandChannel {
public:
    // Bound (security): stdin commands are control text, not assets. Overflow rejects the whole 1 MiB line and is counted.
    static constexpr size_t lineBytes = 1024 * 1024;
    // Bound (security): stalled app delivery retains at most 1024 commands / 16 MiB of raw text plus nodes. Overflow drops new whole commands and is counted.
    static constexpr size_t commandCount = 1024, queuedBytes = 16 * 1024 * 1024;
    std::atomic<uint64_t> rejectedLines{0}, droppedCommands{0};

    void rejectLine() { rejectedLines.fetch_add(1); lineLossPending_.store(true); }
    // Called by the runners even when delivery is disabled. The reader never
    // logs or waits for app delivery, including on an unterminated flood.
    void reportLoss() {
        if (lineLossPending_.exchange(false) && !lineWarned_.exchange(true)) {
#if defined(__cpp_exceptions)
            try {
#endif
                logWarning("Console") << "Rejected whole stdin line(s): capacity 1 MiB; line too long or storage unavailable; total "
                                      << rejectedLines.load();
#if defined(__cpp_exceptions)
            } catch (...) {}
#endif
        }
        if (queueLossPending_.exchange(false) && !queueWarned_.exchange(true)) {
#if defined(__cpp_exceptions)
            try {
#endif
                logWarning("Console") << "Dropped whole stdin command(s): pending capacity 1024 commands / 16 MiB or storage unavailable; total "
                                      << droppedCommands.load();
#if defined(__cpp_exceptions)
            } catch (...) {}
#endif
        }
    }
    bool sendRaw(std::string_view line, trussc::internal::InputStorage::Reallocate allocate = std::realloc) {
        if (line.size() > lineBytes) { rejectLine(); return false; }
        std::unique_lock<std::mutex> lock(mutex_);
        if (count_ == commandCount || line.size() > queuedBytes - bytes_) { lock.unlock(); return drop(); }
        auto* node = static_cast<Node*>(allocate(nullptr, sizeof(Node) + line.size()));
        if (!node) { lock.unlock(); return drop(); }
        node->next = nullptr;
        node->length = line.size();
        if (!line.empty()) std::memcpy(node + 1, line.data(), line.size());
        if (tail_) tail_->next = node;
        else head_ = node;
        tail_ = node;
        ++count_;
        bytes_ += line.size();
        return true;
    }
    bool send(const ConsoleEventArgs& args) { return sendRaw(args.raw); }
    bool tryReceive(ConsoleEventArgs& args) {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!head_) { rearmWarnings(); return false; }
            Node* node = head_;
            head_ = node->next;
            if (!head_) tail_ = nullptr;
            --count_;
            bytes_ -= node->length;
            lock.unlock();
#if defined(__cpp_exceptions)
            try {
#endif
                args = parseLine(std::string(reinterpret_cast<char*>(node + 1), node->length));
#if defined(__cpp_exceptions)
            } catch (const std::bad_alloc&) {
                std::free(node);
                drop();
                continue;
            }
#endif
            std::free(node);
            if (!args.args.empty()) return true;
        }
        return false;
    }
    std::vector<ConsoleEventArgs> receiveAll() {
        std::vector<ConsoleEventArgs> result;
        ConsoleEventArgs args;
        while (tryReceive(args)) result.push_back(std::move(args));
        return result;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (head_) { Node* node = head_; head_ = head_->next; std::free(node); }
        tail_ = nullptr;
        bytes_ = count_ = 0;
        rearmWarnings();
    }
    ~CommandChannel() { clear(); }
    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return count_; }
    size_t bytes() const { std::lock_guard<std::mutex> lock(mutex_); return bytes_; }
    bool empty() const { return size() == 0; }
    bool isClosed() const { return false; }
private:
    struct Node { Node* next; size_t length; };
    bool drop() {
        droppedCommands.fetch_add(1);
        queueLossPending_.store(true);
        return false;
    }
    void rearmWarnings() { lineWarned_.store(false); queueWarned_.store(false); }
    std::atomic<bool> lineWarned_{false}, queueWarned_{false};
    std::atomic<bool> lineLossPending_{false}, queueLossPending_{false};
    mutable std::mutex mutex_;
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    size_t count_ = 0, bytes_ = 0;
};

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
    // Read chunk only; the line/queue bounds above reject and count whole commands.
    char buffer_[4096];
};

// Consume until EOF even while delivery is stopped. Kept separate from the
// raw stream so the delivery boundaries can also be tested deterministically.
inline void readLines(std::istream& input) {
    trussc::internal::readBoundedLines(input, CommandChannel::lineBytes,
        (std::numeric_limits<size_t>::max)(), [](std::string_view line) {
            if (!isRunning().load()) return;
            // Raw reads bypass text-mode CRLF translation on every platform.
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const auto first = line.find_first_not_of(" \t\r\v\f");
            if (first == std::string_view::npos || line[first] == '#') return;
            getChannel().sendRaw(line);
        }, [](bool) { getChannel().rejectLine(); });
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
    internal::getChannel().clear();

    internal::isRunning().store(true);
    // A raw read may block indefinitely; detach now so std::exit() cannot
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
    internal::getChannel().clear();
#endif
}

namespace internal {

/// Process commands in queue
/// Called each loop by the windowed and headless runners
/// Fires events to runtime().events().console if commands exist
inline void processQueue() {
#ifdef __EMSCRIPTEN__
    // No-op on web
    return;
#else
    // Preserve lazy allocation for apps that never opt into stdin delivery.
    if (internal::spawned().load() || internal::isRunning().load())
        internal::getChannel().reportLoss();
    if (!internal::isRunning().load()) {
        return;
    }

    ConsoleEventArgs args;
    while (internal::getChannel().tryReceive(args)) {
        trussc::internal::dispatchConsoleEvent(args);
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
