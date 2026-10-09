#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && \
    (defined(_WIN32) || defined(__linux__) || (defined(__APPLE__) && TARGET_OS_OSX))
#define TC_HAS_CRASH_HANDLER 1
#else
#define TC_HAS_CRASH_HANDLER 0
#endif

// Internal only: one instance in the core, shared with hot-reload guests.
namespace trussc::internal {
#if TC_HAS_CRASH_HANDLER
void installCrashHandler();
void refreshCrashModules() noexcept;
void setCrashLogFile(const std::filesystem::path& path);
void closeCrashLogFile();
#ifdef _WIN32
// Saved by the host's console guard; zero disarms restoration after sapp_run().
void setCrashConsoleCodePages(unsigned output, unsigned input) noexcept;
#endif

// Main-loop bookkeeping only: no allocation, clocks, disk I/O or locks.
// Atomics let a crashing worker read the main thread's latest context.
struct CrashInput {
    std::atomic<unsigned> sequence{0};
    std::atomic<int> type{0}, x{0}, y{0}, detail{0};
    std::atomic<uint64_t> frame{0};
};
struct CrashContext {
    std::atomic<uint64_t> frame{0};
    std::atomic<const char*> phase{"startup"};
    std::atomic<unsigned> inputCount{0};
    CrashInput inputs[8]; // a deliberately small, fixed recent-input ring
};
CrashContext& crashContext();

inline void crashFrame(uint64_t frame) {
    crashContext().frame.store(frame, std::memory_order_relaxed);
}
inline void crashInput(int type, int x, int y, int detail) {
    auto& context = crashContext();
    const unsigned n = context.inputCount.load(std::memory_order_relaxed);
    auto& e = context.inputs[n % 8];
    e.sequence.store(n * 2 + 1);
    e.type.store(type, std::memory_order_relaxed);
    e.x.store(x, std::memory_order_relaxed);
    e.y.store(y, std::memory_order_relaxed);
    e.detail.store(detail, std::memory_order_relaxed);
    // Use the report's main-loop frame; sokol may have advanced its event counter.
    e.frame.store(context.frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
    e.sequence.store(n * 2 + 2);
    context.inputCount.store(n + 1);
}
struct CrashPhaseScope {
    const char* previous;
    explicit CrashPhaseScope(const char* phase)
        : previous(crashContext().phase.load(std::memory_order_relaxed)) {
        crashContext().phase.store(phase, std::memory_order_relaxed);
    }
    ~CrashPhaseScope() { crashContext().phase.store(previous, std::memory_order_relaxed); }
};
#else
inline void installCrashHandler() {}
inline void refreshCrashModules() noexcept {}
inline void setCrashLogFile(const std::filesystem::path&) {}
inline void closeCrashLogFile() {}
inline void crashFrame(uint64_t) {}
inline void crashInput(int, int, int, int) {}
struct CrashPhaseScope { explicit CrashPhaseScope(const char*) {} };
#endif
} // namespace trussc::internal
