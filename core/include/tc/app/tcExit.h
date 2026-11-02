#pragma once
#include <string>
#include <atomic>
#include <chrono>

// Internal, process-wide exit bookkeeping shared by the runtime and backends.
namespace trussc::internal {
int& appExitCode();  // defined in tcGlobal.cpp; shared by host, guests and backends
bool& appSetupCalled();  // defined in tcGlobal.cpp; whether setup ran in this launch
void shutdownRuntime() noexcept;
// macOS only: finish async sheets before their App owners end.
void cancelMacDialogSheets() noexcept;
bool sessionEndCommitted() noexcept;
bool querySessionEnd() noexcept;
void cancelSessionEnd() noexcept;
void commitSessionEnd() noexcept;
void setSessionActivityProbe(bool (*probe)()) noexcept;
// The terminal phase is separate from process termination for compiled probes.
bool endRuntimeForSession() noexcept;
void prepareLogoutWatchdog();
void cancelLogoutWatchdog() noexcept;
void armLogoutWatchdog(float seconds) noexcept;
[[noreturn]] void terminateSessionProcess(int code = 0) noexcept;
// Atomic deadline arbitration shared by the real worker and deterministic tests.
class LogoutDeadline {
public:
    using Clock = std::chrono::steady_clock;
    void reset() noexcept; // run admission only, after the old worker joined
    void arm(float seconds, Clock::time_point now) noexcept;
    bool expire(Clock::time_point now) noexcept;
    void cancel() noexcept;
private:
    enum State { Idle, Preparing, Armed, Disabled, Cancelled, Expired };
    std::atomic<State> state_{Idle};
    std::atomic<Clock::duration::rep> deadline_{0};
};
void setExitReason(const char* reason);
void setExitReasonIfEmpty(const char* reason);
std::string exitReason();
void clearExitReason();
void setExitBlockReason(const std::string& reason);
std::string exitBlockReason();
bool beginExitCleanup();
std::string exitLogMessage(bool clean);
void installWindowExitSignals();
void restoreWindowExitSignals();
int pendingWindowExitSignal();
void cancelWindowExitSignal();
}
