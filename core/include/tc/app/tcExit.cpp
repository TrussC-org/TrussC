#include "tcExit.h"
#include <mutex>
#include <condition_variable>
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
LogoutDeadline::Clock::time_point LogoutDeadline::armedDeadline() const noexcept {
    if (state_.load(std::memory_order_acquire) != Armed) return Clock::time_point::max();
    return Clock::time_point(Clock::duration(deadline_.load(std::memory_order_relaxed)));
}
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
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false; // guarded by mutex
    std::thread thread;
};
// Prepared on main before native session receivers can run. The worker sleeps
// until arm or stop wakes it, then until the atomic deadline.
LogoutWorker& logoutWorker() { static auto* worker = new LogoutWorker; return *worker; }
}
#endif
void prepareLogoutWatchdog() {
#ifndef __EMSCRIPTEN__
    cancelLogoutWatchdog();
    // Prepare storage and the worker at run admission, before constructors or
    // native callbacks can arrive. An idle commit arms atomics and wakes it.
    auto& worker = logoutWorker();
    worker.deadline.reset();
    worker.stop = false;
    worker.thread = std::thread([state = &worker] {
        std::unique_lock<std::mutex> lock(state->mutex);
        while (!state->stop) {
            const auto deadline = state->deadline.armedDeadline();
            if (deadline == LogoutDeadline::Clock::time_point::max()) state->wake.wait(lock);
            else if (state->deadline.expire(LogoutDeadline::Clock::now())) terminateSessionProcess();
            else state->wake.wait_until(lock, deadline);
        }
    });
#endif
}
void cancelLogoutWatchdog() noexcept {
#ifndef __EMSCRIPTEN__
    auto& worker = logoutWorker();
    worker.deadline.cancel();
    {
        std::lock_guard<std::mutex> lock(worker.mutex);
        worker.stop = true;
    }
    worker.wake.notify_all();
    if (worker.thread.joinable()) worker.thread.join();
#endif
}
void armLogoutWatchdog(float seconds) noexcept {
#ifndef __EMSCRIPTEN__
    auto& worker = logoutWorker();
    const auto now = LogoutDeadline::Clock::now();
    worker.deadline.arm(seconds, now);
    if (worker.deadline.expire(now)) terminateSessionProcess();
    // Pass through the mutex so a worker between its check and its wait
    // cannot miss this wakeup.
    { std::lock_guard<std::mutex> lock(worker.mutex); }
    worker.wake.notify_all();
#else
    if (seconds == 0) terminateSessionProcess();
#endif
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
