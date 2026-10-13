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

bool noInput() { return mode == "disabled" || mode == "setup-exit"; }

struct ConsoleApp : App {
    EventListener listener;
    void setup() override {
        appThread = std::this_thread::get_id();
        check(!console::isEnabled() && !console::internal::spawned(),
              "runner does not start a reader before setup");
        listener = events().console.listen([](ConsoleEventArgs& args) {
            check(std::this_thread::get_id() == appThread && isMainThread(),
                  "console callback runs on the main thread");
            ++received;
            if (mode == "lines") {
                const std::vector<std::string> raw{"a b", "c", "partial"};
                const std::vector<std::vector<std::string>> parsed{{"a", "b"}, {"c"}, {"partial"}};
                check(received <= 3 && args.raw == raw[received - 1] &&
                      args.args == parsed[received - 1], "CRLF, LF and partial EOF raw/args");
                if (received == 3) exitApp();
                return;
            }
            check(args.raw == (received == 1 ? "before setup" : "during loop"),
                  "stdin lines arrive in order without duplication");
            check(args.args.size() == 2, "console parses stdin arguments");
            if (received == 1) {
                if (mode == "restart") {
                    console::internal::getChannel().send(console::internal::parseLine("pending"));
                    console::stop();
                    console::stop();
                    check(console::internal::getChannel().empty(), "stop drains pending input");
                    check(!console::internal::getChannel().isClosed(), "stop leaves channel open");
                    // Model a send that passed the running check just before stop.
                    console::internal::getChannel().send(console::internal::parseLine("late"));
                    console::start();
                    console::start();
                    check(console::internal::getChannel().empty(), "restart drains late input");
                    check(console::internal::spawned(), "restart retains the existing reader");
                }
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
        if (mode != "disabled") {
            console::start();
            console::start();
            check(console::isEnabled(), "double start stays enabled");
        }
        if (mode == "setup-exit") exitApp();
    }
    void update() override {
        if (mode == "disabled") {
            check(!console::isEnabled() && !console::internal::spawned(),
                  "app without opt-in starts no reader");
            check(received == 0, "app without opt-in gets no input");
            exitApp();
        } else if (mode == "manual") {
            console::internal::processQueue();
            manualPumped = true;
            if (received == 2) exitApp();
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
        const int before = received;
        console::internal::processQueue();
        check(received == before, "no console event after stop");
    }
};

// Feed one complete line per underflow. The second underflow happens only
// after readLines has consumed/discarded the stopped line, so restart has a
// deterministic boundary without sleeps or assumptions about thread timing.
class RestartBuffer : public std::streambuf {
    int chunk = 0;
    std::string data;
    int_type underflow() override {
        if (chunk == 0) {
            data = "written while stopped\r\n";
        } else if (chunk == 1) {
            check(console::internal::getChannel().empty(), "line read while stopped is discarded");
            console::start();
            data = "再開 café\r\n";
        } else {
            return traits_type::eof();
        }
        ++chunk;
        setg(data.data(), data.data(), data.data() + data.size());
        return traits_type::to_int_type(*gptr());
    }
};

void stoppedLines() {
    console::start();
    console::stop();
    RestartBuffer buffer;
    std::istream input(&buffer);
    console::internal::readLines(input);
    auto queued = console::internal::getChannel().receiveAll();
    check(queued.size() == 1 && queued[0].raw == "再開 café" &&
          queued[0].args == std::vector<std::string>{"再開", "café"},
          "only the restarted line arrives; UTF-8 and CRLF preserved/normalized");
    console::stop();
}

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
    const std::string scenarioName(scenario);
    const bool eofCase = scenarioName == "lines";
    if (eofCase) {
        require(writeFd(input[1], "a b\r\nc\npartial", 14) == 14, "write mixed line endings and partial line");
    } else if (scenarioName != "std-exit" && scenarioName != "stop-exit" &&
               scenarioName != "stopped-lines") {
        writeLine(input[1], "before setup\n");
    }
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
    if (eofCase) closeFd(input[1]);
    // Bound a broken reader/pump/teardown without leaving an orphan holding
    // the suite's output pipe open. This is a hang guard, not a speed check
    // with no elapsed-time correctness assertion.
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
    // Except for the explicit EOF fixture, stdin stays open until exit.
    // Correctness depends on exit/delivery state, not elapsed-time checks.
    if (!eofCase) closeFd(input[1]);
    closeFd(output[0]);
    std::printf("PASS: %s (%s)\n", scenario, eofCase ? "EOF delivered" : "exited with stdin writer open");
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child") {
        mode = argv[2];
        if (mode == "std-exit" || mode == "stop-exit") {
            console::start();
            if (mode == "stop-exit") console::stop();
            std::exit(0);
        }
        if (mode == "stopped-lines") {
            stoppedLines();
            return failures ? 1 : 0;
        }
        const int result = runHeadlessApp<ConsoleApp>();
        check(result == 0 && exits == 1 && cleanups == 1, "runner completes lifecycle once");
        const int expected = noInput() ? 0 : (mode == "lines" ? 3 : 2);
        check(received == expected, "expected total console callbacks");
        check(!console::isEnabled(), "console remains disabled after cleanup");
        if (mode == "manual") check(manualPumped, "manual update pump ran alongside runner pump");
        if (mode == "sigint" || mode == "sigterm")
            check(internal::exitReason() == mode, "signal reached normal cleanup path");
        console::internal::processQueue();
        check(received == expected, "no callbacks after runner returns");
        return failures ? 1 : 0;
    }
    for (const char* scenario : {"opt-in", "manual", "disabled", "restart", "stopped-lines",
                                 "lines", "std-exit", "stop-exit", "sigint", "sigterm", "setup-exit"}) {
        runChild(argv[0], scenario);
    }
    return 0;
}
