#pragma once

// Run a command and receive its stdout and stderr as ONE ordered byte stream
// (#1240). Both streams of the child are the same pipe, so a compiler
// diagnostic (stderr) arrives in the right place between the build tool's
// progress lines (stdout). No shell is involved.

#include <cstdio>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace projectOps {
inline constexpr auto stopGracePeriod = std::chrono::seconds(3);
enum class ProcessState { Idle, Running, StopRequested, StillRunning, Exited };

// One handle per operation. Calls may come from the UI thread while the
// operation runs on its worker. Neither request escalates automatically.
class CancelHandle {
public:
    void requestStop() { stopRequested_ = true; }
    void forceStop() { stopRequested_ = true; forceRequested_ = true; }
    bool stopRequested() const { return stopRequested_; }
    bool forceRequested() const { return forceRequested_; }
    ProcessState state() const { return state_; }
private:
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> forceRequested_{false};
    std::atomic<ProcessState> state_{ProcessState::Idle};
    friend struct ProcessRunner;
};

struct ProcessOptions {
    CancelHandle* cancel = nullptr;
    std::string workingDirectory;
    bool ownGroup = true;
    // CLI only: forward POSIX signals; retain the Windows console group.
    bool forwardSignals = false;
    bool inheritOutput = false;
    bool hideConsole = false; // Known console tools only; apps keep their normal show state.
#ifdef _WIN32
    std::string windowsCommandLine; // Existing generator shell commands, already quoted for cmd.exe.
#endif
    FILE* stderrFile = nullptr;
    const bool* outputLimitExceeded = nullptr; // Legacy bounded archive reader.
};
struct ProcessResult {
    int exitCode = -1;
    bool stopped = false;
    int forwardedSignal = 0; // POSIX CLI restores the terminal's exit status.
};
ProcessResult runProcess(const std::vector<std::string>& argv,
    const std::function<void(const char*, size_t)>& onData = {},
    const ProcessOptions& options = {});
}

// CLI passthrough uses the same launcher, retaining its foreground group and
// inherited streams (including interactive terminal input).
int runInherited(const std::vector<std::string>& argv);

// Calls onData with each piece of output as it arrives. Returns the child's
// exit code (-1 when it cannot be started or ended by a signal).
//
// Bounded wait: once the direct child has exited, only the output already in
// the pipe is read, so a background process that kept the pipe open cannot
// hold trusscli.
// When stderrFile is supplied, stderr goes there instead of the output pipe.
// Setting cancel explicitly requests immediate termination for the legacy
// bounded archive reader. Interactive Stop uses CancelHandle::requestStop().
int runCaptured(const std::vector<std::string>& argv,
                const std::function<void(const char*, size_t)>& onData,
                FILE* stderrFile = nullptr, const bool* cancel = nullptr);
