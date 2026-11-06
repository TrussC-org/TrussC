#pragma once

// =============================================================================
// TrussC Process - run an external program and read its output
//
// Two forms, both without a shell (arguments are passed as-is, no quoting or
// globbing; args[0] is looked up on PATH):
//
//   runProcess(args)  synchronous, like openFrameworks' ofSystem(): blocks the
//                     calling thread until the program exits and returns its
//                     exit code and output.
//
//   Process           asynchronous: start() returns at once, a worker thread
//                     reads the output and fires onLine per line and onExit
//                     once. Both fire on the worker thread; listen with
//                     Deliver::Main to touch the Node tree / GPU:
//
//     process.onLine.listen([this](auto& e) { status = e.line; }, Deliver::Main);
//     process.onExit.listen([this](auto& e) { if (e.exitCode == 0) reload(); },
//                           Deliver::Main);
//     process.start({"node", "render.js", "--out", path});
//
// Platforms: macOS, Windows, Linux. Elsewhere (Web, iOS, Android) start()
// returns false and runProcess() returns started == false, with one error log.
//
// Output: stdout and stderr are read from separate pipes, so neither can fill
// up and block the program. Lines end at "\n", "\r\n" or a lone "\r" (progress
// bars that redraw one line count as one line per redraw); the terminator is
// not part of the line. A line longer than 1 MiB is split.
// The program's stdin is the null device.
//
// Exit codes: the program's exit status. On macOS/Linux a program ended by a
// signal reports 128 + the signal number (as a shell does).
//
// Windows: args[0] is found on PATH only as an .exe. Batch scripts (npm.cmd,
// foo.bat) need cmd: {"cmd", "/c", "npm", "run", "build"}. Arguments are UTF-8
// and are quoted for the usual argv parsing (CommandLineToArgvW / MSVC CRT).
// Console programs run without opening a console window.
//
// Child processes: the program and every process it starts are killed
// together (POSIX: its own process group; Windows: a job object). A program
// that leaves its process group / job on purpose is not followed.
// =============================================================================

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "tcFileIO.h"
#include "../events/tcEvent.h"

namespace trussc {

// -----------------------------------------------------------------------------
// Synchronous
// -----------------------------------------------------------------------------

struct ProcessResult {
    int exitCode = -1;      // -1 when the program could not be started
    std::string output;     // stdout and stderr lines in arrival order, each ending in '\n'
    bool started = false;   // false: not found, not executable, or unsupported platform
};

// Blocks until the program (not its children) exits. Do not call it from
// draw()/update() for anything slow: the frame waits too. Use Process instead.
ProcessResult runProcess(const std::vector<std::string>& args,
                         const fs::path& workingDir = {});

// -----------------------------------------------------------------------------
// Asynchronous
// -----------------------------------------------------------------------------

struct ProcessLineEventArgs {
    std::string line;
    bool isStderr = false;
};

struct ProcessExitEventArgs {
    int exitCode = -1;
    float seconds = 0;      // time from start() to exit
    bool killed = false;    // ended by kill() or by destroying the Process
};

namespace internal { class ProcessRunner; }

class Process {
public:
    // Fired on the worker thread. onLine for every line of output (all lines
    // come before onExit); onExit once per successful start().
    Event<ProcessLineEventArgs> onLine;
    Event<ProcessExitEventArgs> onExit;

    Process();
    // Kills the program (and its children) unless setKillOnDestroy(false),
    // then waits for the worker thread. With setKillOnDestroy(false) the
    // destructor waits until the program exits by itself.
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Starts the program. Returns false (and logs why) when it is already
    // running, could not be started, or the platform has no processes.
    // A finished Process can be started again. Not from its own listeners
    // that run on the worker thread (Deliver::Inline): that returns false.
    bool start(const std::vector<std::string>& args, const fs::path& workingDir = {});

    // True from a successful start() until the program has exited and its
    // remaining output has been delivered (just before onExit fires).
    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    // Ends the program and its children. POSIX: SIGTERM, then SIGKILL if it
    // is still running 3 seconds later. Windows: terminated at once.
    // Returns immediately; onExit follows with killed == true. No-op when
    // nothing runs.
    void kill();

    // Blocks until the program has exited and onExit has fired (inline
    // listeners have run; Deliver::Main ones are queued). Returns the exit
    // code, or -1 if nothing was started. Not from the worker thread.
    int waitForExit();

    void setKillOnDestroy(bool kill) { killOnDestroy_ = kill; }
    bool getKillOnDestroy() const { return killOnDestroy_; }

    // Exit code of the last finished run, -1 before the first one ends.
    int getExitCode() const { return exitCode_.load(std::memory_order_acquire); }

private:
    void joinWorker();

    std::mutex mutex_;  // guards runner_ and worker_
    std::unique_ptr<internal::ProcessRunner> runner_;
    std::thread worker_;
    std::atomic<std::thread::id> workerId_{};  // set while the worker runs (refuses self-waits)
    std::atomic<bool> running_{false};
    std::atomic<bool> killRequested_{false};
    std::atomic<int> exitCode_{-1};
    bool killOnDestroy_ = true;
};

} // namespace trussc
