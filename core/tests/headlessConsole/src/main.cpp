// #756: real stdin delivery and shutdown with the pipe writer still open.
#define TRUSSC_SHOW_CONSOLE
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tc;

namespace {
std::string mode;
int failures = 0;
int received = 0;
int exits = 0;
int cleanups = 0;
bool manualPumped = false;
std::thread::id appThread;

void check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL [%s]: %s\n", mode.c_str(), message);
        ++failures;
    }
}

struct ConsoleApp : App {
    EventListener listener;
    void setup() override {
        appThread = std::this_thread::get_id();
        check(!console::isEnabled(), "automatic start follows setup");
        listener = events().console.listen([](ConsoleEventArgs& args) {
            check(std::this_thread::get_id() == appThread && isMainThread(),
                  "console callback runs on the main thread");
            ++received;
            check(args.raw == (received == 1 ? "before setup" : "during loop"),
                  "stdin lines arrive in order without duplication");
            check(args.args.size() == 2, "console parses stdin arguments");
            if (received == 1) {
                // Parent sends the next line only after a real callback.
                std::puts("NEXT");
                std::fflush(stdout);
            } else {
                if (mode == "sigint" || mode == "sigterm") {
#ifdef _WIN32
                    // Real Ctrl+C delivery needs a Windows console session.
                    headless::consoleHandler(mode == "sigint" ? CTRL_C_EVENT : CTRL_CLOSE_EVENT);
#else
                    std::raise(mode == "sigint" ? SIGINT : SIGTERM);
#endif
                } else if (mode != "manual") {
                    exitApp();
                }
            }
        });
        if (mode == "manual" || mode == "manual-optout") {
            console::start();
            console::start();
            check(console::isEnabled(), "double manual start stays enabled");
        }
        if (mode == "optout" || mode == "manual-optout") {
            check(console::internal::getChannel().send(
                      console::internal::parseLine("queued before opt-out")),
                  "seed pending input before setup opt-out");
            console::stop();
            console::stop();
            check(!console::isEnabled(), "double setup stop disables console");
        }
        if (mode == "setup-exit") exitApp();
    }
    void update() override {
        if (mode == "optout" || mode == "manual-optout") {
            check(!console::isEnabled(), "automatic start respects setup opt-out");
            console::internal::processQueue();
            check(received == 0, "setup opt-out delivers no queued input");
            exitApp();
        } else {
            const bool enabled = console::isEnabled();
            check(enabled, "runner enables console before update");
            if (!enabled) exitApp();
            if (mode == "manual") {
                console::internal::processQueue();
                manualPumped = true;
                if (received == 2) exitApp();
            }
        }
    }
    void exit() override {
        ++exits;
        check(!console::isEnabled(), "runner stops console before app exit");
        if (mode == "manual") {
            console::stop();
            console::stop();
        }
    }
    void cleanup() override {
        ++cleanups;
        check(exits == 1, "exit precedes cleanup");
        check(!console::isEnabled(), "console disabled in cleanup");
        // Even work queued before stop cannot be delivered after stop.
        const int before = received;
        check(!console::internal::getChannel().send(
                  console::internal::parseLine("after stop")),
              "stopped reader channel rejects later input");
        console::internal::processQueue();
        check(received == before, "no console event after stop");
    }
};

#ifdef _WIN32
int makePipe(int* descriptors) { return _pipe(descriptors, 4096, _O_BINARY); }
int copyFd(int descriptor) { return _dup(descriptor); }
int replaceFd(int source, int target) { return _dup2(source, target); }
int closeFd(int descriptor) { return _close(descriptor); }
int readFd(int descriptor, char* buffer, unsigned count) { return _read(descriptor, buffer, count); }
int writeFd(int descriptor, const char* buffer, unsigned count) { return _write(descriptor, buffer, count); }
#else
int makePipe(int* descriptors) { return pipe(descriptors); }
int replaceFd(int source, int target) { return dup2(source, target); }
int closeFd(int descriptor) { return close(descriptor); }
int readFd(int descriptor, char* buffer, unsigned count) { return int(read(descriptor, buffer, count)); }
int writeFd(int descriptor, const char* buffer, unsigned count) { return int(write(descriptor, buffer, count)); }
#endif

void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void writeLine(int descriptor, const char* line) {
    std::string text(line);
#ifdef _WIN32
    // Binary pipe writes exercise the reader's Windows CRLF translation.
    text.insert(text.size() - 1, "\r");
#endif
    const unsigned length = unsigned(text.size());
    require(writeFd(descriptor, text.data(), length) == int(length), "write child stdin");
}

void runChild(const char* executable, const char* scenario) {
    int input[2], output[2];
    require(makePipe(input) == 0 && makePipe(output) == 0, "create child pipes");
    writeLine(input[1], "before setup\n");
    std::fflush(nullptr);
#ifdef _WIN32
    // _spawn passes the CRT descriptors, including redirected stdin/stdout.
    // Keep only the child's read/write ends inheritable; the parent retains
    // the stdin writer until _cwait observes process exit.
    require(SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(input[1])),
                                 HANDLE_FLAG_INHERIT, 0) != 0, "stdin writer not inherited");
    require(SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(output[0])),
                                 HANDLE_FLAG_INHERIT, 0) != 0, "stdout reader not inherited");
    const int savedInput = copyFd(0), savedOutput = copyFd(1);
    require(savedInput >= 0 && savedOutput >= 0, "save parent stdio");
    require(replaceFd(input[0], 0) >= 0 && replaceFd(output[1], 1) >= 0, "redirect child stdio");
    const intptr_t child = _spawnl(_P_NOWAIT, executable, executable, "--child", scenario, nullptr);
    require(replaceFd(savedInput, 0) >= 0 && replaceFd(savedOutput, 1) >= 0, "restore parent stdio");
    closeFd(savedInput);
    closeFd(savedOutput);
    require(child != -1, "spawn child");
#else
    const pid_t child = fork();
    require(child >= 0, "fork child");
    if (child == 0) {
        closeFd(input[1]);
        closeFd(output[0]);
        if (replaceFd(input[0], 0) < 0 || replaceFd(output[1], 1) < 0) _exit(1);
        closeFd(input[0]);
        closeFd(output[1]);
        execl(executable, executable, "--child", scenario, static_cast<char*>(nullptr));
        _exit(1);
    }
#endif
    closeFd(input[0]);
    closeFd(output[1]);
    // Bound a broken reader/pump/teardown without leaving an orphan holding
    // the suite's output pipe open. This is a hang guard, not a speed check
    // (the seven successful cases together take less than a second locally).
    std::mutex watchdogMutex;
    std::condition_variable watchdogWake;
    bool completed = false;
    std::thread watchdog([&] {
        std::unique_lock lock(watchdogMutex);
        if (!watchdogWake.wait_for(lock, std::chrono::seconds(60), [&] { return completed; })) {
            std::fprintf(stderr, "FAIL: child hung in %s\n", scenario);
#ifdef _WIN32
            TerminateProcess(reinterpret_cast<HANDLE>(child), 1);
#else
            kill(child, SIGKILL);
#endif
        }
    });
    std::string line;
    char byte;
    int count;
    while ((count = readFd(output[0], &byte, 1)) != 0) {
#ifndef _WIN32
        if (count < 0 && errno == EINTR) continue;
#endif
        require(count == 1, "read child stdout");
        if (byte == '\n') {
            if (line == "NEXT") writeLine(input[1], "during loop\n");
            line.clear();
        } else if (byte != '\r') {
            line += byte;
        }
    }
    // Disarm before reaping the PID (or closing the Windows process handle).
    {
        std::lock_guard lock(watchdogMutex);
        completed = true;
    }
    watchdogWake.notify_one();
    watchdog.join();
    int status = 0;
#ifdef _WIN32
    require(_cwait(&status, child, 0) != -1 && status == 0, scenario);
#else
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, scenario);
#endif
    // No EOF was sent: process termination must not wait for getline.
    // Correctness depends on exit/delivery state, not elapsed-time checks.
    closeFd(input[1]);
    closeFd(output[0]);
    std::printf("PASS: %s (exited with stdin writer open)\n", scenario);
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child") {
        mode = argv[2];
        const int result = runHeadlessApp<ConsoleApp>();
        check(result == 0 && exits == 1 && cleanups == 1, "runner completes lifecycle once");
        const bool noInput = mode == "optout" || mode == "manual-optout" || mode == "setup-exit";
        check(received == (noInput ? 0 : 2), "expected total console callbacks");
        check(!console::isEnabled(), "console remains disabled after cleanup");
        if (mode == "manual") check(manualPumped, "manual update pump ran alongside runner pump");
        if (mode == "sigint" || mode == "sigterm")
            check(internal::exitReason() == mode, "signal reached normal cleanup path");
        console::internal::processQueue();
        check(received == (noInput ? 0 : 2), "no callbacks after runner returns");
        return failures ? 1 : 0;
    }
    for (const char* scenario : {"automatic", "manual", "optout", "manual-optout",
                                 "sigint", "sigterm", "setup-exit"}) {
        runChild(argv[0], scenario);
    }
    return 0;
}
