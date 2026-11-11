#pragma once

// =============================================================================
// TrussC Process - run an external program and read its output
//
// Two forms, both without a shell (arguments are passed as-is, no quoting or
// globbing; embedded NUL is rejected; args[0] is looked up on PATH):
//
//   runProcess(args)  synchronous, like openFrameworks' ofSystem(): blocks the
//                     calling thread until the program exits and returns its
//                     exit code and output.
//
//   Process           asynchronous: startAsync() returns at once, a worker thread
//                     reads the output and fires onLine per line and onExit
//                     once. Listeners run on the worker thread by default.
//                     Do not touch Nodes or the GPU there; use Deliver::Main:
//
//     // App members: mutex outputMutex; vector<string> output;
//     // Process process; EventListener lineListener, exitListener;
//     lineListener = process.onLine.listen([this](auto& e) {
//         lock_guard<mutex> lock(outputMutex);
//         output.push_back(e.line);
//     });
//     exitListener = process.onExit.listen([this](auto& e) {
//         if (e.exitCode == 0) reload();
//     }, Deliver::Main);
//     process.startAsync({"node", "render.js", "--out", path});
//
// Platforms: macOS, Windows, Linux. Elsewhere (Web, iOS, Android) startAsync()
// returns false and runProcess() returns started == false, with one error log.
//
// Output: stdout and stderr are read from separate pipes, so neither can fill
// up and block the program. Lines end at "\n", "\r\n" or a lone "\r" (progress
// bars that redraw one line count as one line per redraw); the terminator is
// not part of the line. A line longer than 1 MiB is split.
// The program's stdin is the null device. The environment is inherited, with
// no per-child override. Windows output bytes are not converted from the
// child's code page.
//
// Exit codes: the program's exit status. On macOS/Linux a program ended by a
// signal reports 128 + the signal number (as a shell does).
//
// Windows: args[0] is found on PATH only as an .exe. Batch scripts (npm.cmd,
// foo.bat) need cmd: {"cmd", "/c", "npm", "run", "build"}. Arguments are UTF-8
// and are quoted for the usual argv parsing (CommandLineToArgvW / MSVC CRT).
// Embedded quotes are escaped for argv parsing; cmd /c uses its own parser and does not receive them verbatim.
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

#include "tcAnnotations.h"
#include "tcFileIO.h"
#include "../events/tcEvent.h"

namespace trussc {

// -----------------------------------------------------------------------------
// Synchronous
// -----------------------------------------------------------------------------

struct TC_LUA_SKIP ProcessResult {
    int exitCode = -1;      // -1 when the program could not be started
    std::string output;     // stdout and stderr lines in arrival order, each ending in '\n'
    bool started = false;   // false: not found, not executable, or unsupported platform
};

// Blocks until the program (not its children) exits. Do not call it from
// draw()/update() for anything slow: the frame waits too. Use Process instead.
TC_PLATFORMS("macos,windows,linux") TC_LUA_SKIP ProcessResult runProcess(const std::vector<std::string>& args,
                                     const fs::path& workingDir = {});

// -----------------------------------------------------------------------------
// Asynchronous
// -----------------------------------------------------------------------------

struct TC_LUA_SKIP ProcessLineEventArgs {
    std::string line;
    bool isStderr = false;
};

struct TC_LUA_SKIP ProcessExitEventArgs {
    int exitCode = -1;
    float seconds = 0;      // time from startAsync() to exit
    bool killed = false;    // ended by kill(); destruction clears listeners, so no onExit is delivered
};

namespace internal { class ProcessRunner; }

class TC_PLATFORMS("macos,windows,linux") TC_LUA_SKIP Process {
    struct State;
    std::shared_ptr<State> state_;  // also held by the worker across callbacks
public:
    // THREADING: these events fire on the worker thread. A listener that
    // touches Nodes, GPU resources, or unguarded app state must opt into
    // main-thread delivery: listener = process.onLine.listen(fn, Deliver::Main).
    // Deliver::Main copies the payload and runs the listener at the start of
    // the next frame (see tcEvent.h). Plain listen(fn) runs inline on the worker.
    // onLine for every line (all lines come before onExit); onExit once per
    // successful startAsync(). Keep the returned EventListener tokens.
    // setMainThreadDeliveryLimit(count) optionally bounds pending Main copies per event; 0 is unlimited.
    Event<ProcessLineEventArgs>& onLine;
    Event<ProcessExitEventArgs>& onExit;

    Process();
    // Kills the program (and its children) unless setKillOnDestroy(false),
    // then waits for the worker thread. With setKillOnDestroy(false) the
    // destructor waits until the program exits by itself.
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Starts without waiting; listeners run on the worker thread by default.
    // Call from the main thread, including before runApp()/runHeadlessApp().
    // Returns false (and logs why) when it is already
    // running, could not be started, or the platform has no processes.
    // A finished Process can be started again. Not from its own listeners
    // that run on the worker thread (Deliver::Inline): that returns false.
    bool startAsync(const std::vector<std::string>& args, const fs::path& workingDir = {});

    // True from a successful startAsync() until the program has exited and its
    // remaining output has been dispatched (just before onExit fires).
    bool isRunning() const;

    // Ends the program and its children. POSIX: SIGTERM, then SIGKILL if its
    // group is still present 3 seconds later. Windows: terminated at once.
    // Returns immediately; onExit follows with killed == true. No-op when
    // nothing runs.
    void kill();

    // Blocks until the program has exited and onExit has fired (inline
    // listeners have run; Deliver::Main ones are queued). Returns the exit
    // code, or -1 if nothing was started. Not from the worker thread.
    int waitForExit();

    // If the app is hard-killed, Windows ends its children (kill-on-close job); POSIX leaves the child group running.
    void setKillOnDestroy(bool kill) { killOnDestroy_ = kill; }
    bool getKillOnDestroy() const { return killOnDestroy_; }

    // Exit code of the last finished run, -1 before the first one ends.
    int getExitCode() const;

private:
    void joinWorker();

    std::mutex mutex_;  // guards starting and joining the worker
    std::thread worker_;
    bool killOnDestroy_ = true;
};

} // namespace trussc
