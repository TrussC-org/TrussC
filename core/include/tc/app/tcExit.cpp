#include "tcExit.h"
#include "internal/tcSessionTrace.h"
#include <mutex>
#include <atomic>
#include <csignal>
#include <cmath>
#include <thread>
#include <memory>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace trussc::internal {
void LogoutDeadline::reset() noexcept { state_.store(Idle, std::memory_order_release); }
void LogoutDeadline::arm(float seconds, Clock::time_point now) noexcept {
    auto expected = Idle;
    if (!state_.compare_exchange_strong(expected, Preparing, std::memory_order_acq_rel)) return;
    // A very large finite value still has its numeric duration. Saturate only
    // the clock representation, avoiding an overflowing duration conversion.
    const auto available = Clock::time_point::max() - now;
    const auto deadline = double(seconds) >= std::chrono::duration<double>(available).count()
        ? Clock::time_point::max() : now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    deadline_.store(deadline.time_since_epoch().count(), std::memory_order_relaxed);
    expected = Preparing;
    state_.compare_exchange_strong(expected, std::isinf(seconds) ? Disabled : Armed, std::memory_order_release);
}
bool LogoutDeadline::expire(Clock::time_point now) noexcept {
    if (state_.load(std::memory_order_acquire) != Armed || now.time_since_epoch().count() < deadline_.load(std::memory_order_relaxed)) return false;
    auto expected = Armed;
    return state_.compare_exchange_strong(expected, Expired, std::memory_order_acq_rel);
}
void LogoutDeadline::cancel() noexcept { state_.exchange(Cancelled, std::memory_order_acq_rel); }
[[noreturn]] void terminateSessionProcess(int code) noexcept {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    _exit(code);
#else
    _exit(code);
#endif
}
#ifndef __EMSCRIPTEN__
namespace {
struct LogoutWorker {
    LogoutDeadline deadline;
    std::atomic<bool> stop{false};
    std::thread thread;
};
// Prepared on main before native session receivers can run. The worker only
// observes the atomic deadline and stop flag.
LogoutWorker& logoutWorker() { static auto* worker = new LogoutWorker; return *worker; }
}
#endif
void prepareLogoutWatchdog() {
    TC_STRACE("watchdog prepare");
#ifndef __EMSCRIPTEN__
    cancelLogoutWatchdog();
    // Prepare storage and the worker at run admission, before constructors or
    // native callbacks can arrive. An idle commit only arms atomics.
    auto& worker = logoutWorker();
    worker.deadline.reset();
    worker.stop.store(false, std::memory_order_release);
    worker.thread = std::thread([state = &worker] {
        while (!state->stop.load(std::memory_order_acquire)) {
            if (state->deadline.expire(LogoutDeadline::Clock::now())) {
                TC_STRACE("watchdog thread expired: terminateSessionProcess");
                terminateSessionProcess();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
#endif
}
void cancelLogoutWatchdog() noexcept {
#ifndef __EMSCRIPTEN__
    auto& worker = logoutWorker();
    worker.deadline.cancel();
    worker.stop.store(true, std::memory_order_release);
    if (worker.thread.joinable()) worker.thread.join();
#endif
}
void armLogoutWatchdog(float seconds) noexcept {
    TC_STRACE("watchdog arm begin");
#ifndef __EMSCRIPTEN__
    auto& worker = logoutWorker();
    const auto now = LogoutDeadline::Clock::now();
    worker.deadline.arm(seconds, now);
    if (worker.deadline.expire(now)) {
        TC_STRACE("watchdog immediate expiry: terminateSessionProcess");
        terminateSessionProcess();
    }
#else
    if (seconds == 0) terminateSessionProcess();
#endif
    TC_STRACE("watchdog arm end");
}
namespace {
std::mutex exitMutex;
std::string reason;
std::string blockReason;
std::atomic<bool> cleaning{false};
#if (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || (defined(__APPLE__) && TARGET_OS_OSX)
static_assert(std::atomic<sig_atomic_t>::is_always_lock_free);
std::atomic<sig_atomic_t> pendingSignal{0};
sig_atomic_t deliveredSignal = 0;
struct sigaction oldTerm{}, oldInt{};
bool signalsInstalled = false;
void onExitSignal(int sig) {
    // No logging, allocation, callbacks or locks in a signal handler. Keep the
    // flag until the main thread cancels the pending request.
    if (pendingSignal.exchange(sig, std::memory_order_relaxed)) _exit(128 + sig);
}
#endif
}
void setExitReason(const char* value) {
    std::lock_guard<std::mutex> lock(exitMutex);
    if (!cleaning) reason = value ? value : "";
}
void setExitReasonIfEmpty(const char* value) {
    std::lock_guard<std::mutex> lock(exitMutex);
    if (!cleaning && reason.empty()) reason = value;
}
std::string exitReason() {
    std::lock_guard<std::mutex> lock(exitMutex);
    return reason;
}
void clearExitReason() { setExitReason(nullptr); }
void setExitBlockReason(const std::string& value) { blockReason = value; }
std::string exitBlockReason() { return blockReason; }
bool beginExitCleanup() {
    std::lock_guard<std::mutex> lock(exitMutex);
    if (cleaning) return false;
    cleaning = true;
    return true;
}
std::string exitLogMessage(bool clean) {
    const auto why = exitReason();
#ifdef _WIN32
    const auto pid = _getpid();
#else
    const auto pid = getpid();
#endif
    return std::string("exit: ") + (clean ? "clean" : "begin")
        + (why.empty() ? "" : " reason=" + why)
        + (clean ? " code=" + std::to_string(appExitCode()) : "") + " pid=" + std::to_string(pid);
}
void installWindowExitSignals() {
#if (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || (defined(__APPLE__) && TARGET_OS_OSX)
    if (signalsInstalled) return;
    pendingSignal = 0;
    deliveredSignal = 0;
    struct sigaction action{};
    action.sa_handler = onExitSignal;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGINT);
    sigaction(SIGTERM, &action, &oldTerm);
    sigaction(SIGINT, &action, &oldInt);
    signalsInstalled = true;
#endif
}
void restoreWindowExitSignals() {
#if (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || (defined(__APPLE__) && TARGET_OS_OSX)
    if (!signalsInstalled) return;
    sigaction(SIGTERM, &oldTerm, nullptr);
    sigaction(SIGINT, &oldInt, nullptr);
    signalsInstalled = false;
#endif
}
int pendingWindowExitSignal() {
#if (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || (defined(__APPLE__) && TARGET_OS_OSX)
    // Acknowledge delivery without clearing the pending request: another
    // signal must still escalate while listeners or cleanup are running.
    const sig_atomic_t value = pendingSignal.load(std::memory_order_relaxed);
    if (value == deliveredSignal) return 0;
    deliveredSignal = value;
    return value;
#else
    return 0;
#endif
}
void cancelWindowExitSignal() {
#if (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || (defined(__APPLE__) && TARGET_OS_OSX)
    // Main thread only. The next signal starts a new cancellable request.
    pendingSignal.store(0, std::memory_order_relaxed);
    deliveredSignal = 0;
#endif
}
}
