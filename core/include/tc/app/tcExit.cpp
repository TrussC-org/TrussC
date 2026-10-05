#include "tcExit.h"
#include <mutex>
#include <atomic>
#include <csignal>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace trussc::internal {
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
    // flag after a veto so a second signal can also stop a stuck editor.
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
        + (clean ? " code=0" : "") + " pid=" + std::to_string(pid);
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
    // Main-thread acknowledgement is separate from the signal flag: a veto
    // must not result in repeated requests on every frame.
    const sig_atomic_t value = pendingSignal.load(std::memory_order_relaxed);
    if (value == deliveredSignal) return 0;
    deliveredSignal = value;
    return value;
#else
    return 0;
#endif
}
}
