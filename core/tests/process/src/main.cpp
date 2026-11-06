// =============================================================================
// core/tests/process — runProcess() / Process (#1271)
//
// Headless, console, exit code = pass/fail. The child programs are this test
// binary itself, started again with "--child <mode>" (inside allCoreTests the
// test name goes first), so no outside program is needed on any platform.
//
// Guards:
//   - runProcess(): exit code, stdout and stderr lines, "\r\n" / lone "\r"
//     line endings and an unterminated last line; arguments with spaces,
//     quotes, trailing backslashes and empty ones reach the child unchanged;
//     workingDir; a missing program reports started == false.
//   - Output larger than a pipe buffer on both streams does not deadlock.
//   - Process: onLine per line and one onExit with the exit code; a second
//     start() while running is refused; kill() ends a sleeping child (and is
//     reported as killed); the destructor kills by default; Deliver::Main
//     listeners run when the main-thread queue is drained.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

using namespace tc;

namespace {

int failures = 0;
std::string selfPath;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}

std::vector<std::string> child(std::initializer_list<std::string> rest) {
    std::vector<std::string> args{selfPath};
#ifdef TC_CORE_TEST_NAME
    args.push_back(TC_CORE_TEST_NAME);
#endif
    args.push_back("--child");
    args.insert(args.end(), rest);
    return args;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Child side
// ---------------------------------------------------------------------------

int runChildMode(int argc, char** argv) {
    const std::string mode = argv[0];
    if (mode == "echo") {
#ifdef _WIN32
        // Text mode would turn the "\r\n" below into "\r\r\n".
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::fputs("out1\n", stdout);
        std::fflush(stdout);
        std::fputs("err1\n", stderr);
        std::fflush(stderr);
        std::fputs("a\r\nb\rc", stdout);  // no terminator on the last line
        std::fflush(stdout);
        return 7;
    }
    if (mode == "args") {
        for (int i = 1; i < argc; ++i) std::printf("[%s]\n", argv[i]);
        return 0;
    }
    if (mode == "cwd") {
        std::printf("%s\n", fs::current_path().string().c_str());
        return 0;
    }
    if (mode == "spam") {
        const std::string line(1023, 'x');
        for (int i = 0; i < 1024; ++i) {  // 1 MiB on each stream
            std::fprintf(stdout, "%s\n", line.c_str());
            std::fprintf(stderr, "%s\n", line.c_str());
        }
        return 0;
    }
    if (mode == "sleep") {
        std::puts("ready");
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    std::fprintf(stderr, "unknown child mode %s\n", mode.c_str());
    return 99;
}

// ---------------------------------------------------------------------------
// Parent side
// ---------------------------------------------------------------------------

void testRunProcess() {
    const auto result = runProcess(child({"echo"}));
    check("runProcess starts", result.started);
    check("runProcess exit code", result.exitCode == 7);
    check("runProcess stdout line", contains(result.output, "out1\n"));
    check("runProcess stderr line", contains(result.output, "err1\n"));
    // Streams interleave by arrival (the unterminated "c" is flushed last), so
    // check the stdout order with the stderr line taken out.
    std::string stdoutOnly = result.output;
    if (const auto at = stdoutOnly.find("err1\n"); at != std::string::npos) stdoutOnly.erase(at, 5);
    check("runProcess splits \\r\\n and \\r",
          stdoutOnly == "out1\na\nb\nc\n" && !contains(result.output, "\r"));

    const auto args = runProcess(child({"args", "has space", "quote\"inside", "trail\\", "",
                                        "back\\\\\"slash", "plain"}));
    check("arguments reach the child unchanged",
          args.exitCode == 0 &&
              args.output == "[has space]\n[quote\"inside]\n[trail\\]\n[]\n[back\\\\\"slash]\n[plain]\n");
    if (args.exitCode != 0 || !contains(args.output, "[plain]")) std::printf("  got: %s\n", args.output.c_str());

    const fs::path dir = fs::temp_directory_path();
    const auto cwd = runProcess(child({"cwd"}), dir);
    std::string printed = cwd.output;
    while (!printed.empty() && printed.back() == '\n') printed.pop_back();
    std::error_code error;
    check("workingDir", cwd.exitCode == 0 && fs::equivalent(printed, dir, error));

    const auto missing = runProcess({"tc-process-test-no-such-program"});
    check("missing program is not started", !missing.started && missing.exitCode == -1);

    const auto spam = runProcess(child({"spam"}));
    check("large output on both streams", spam.exitCode == 0 && spam.output.size() == 2 * 1024 * 1024);
}

void testProcess() {
    std::mutex mutex;
    std::vector<ProcessLineEventArgs> lines;
    int exits = 0;
    ProcessExitEventArgs exitArgs;

    Process process;
    auto lineListener = process.onLine.listen([&](ProcessLineEventArgs& e) {
        std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(e);
    });
    auto exitListener = process.onExit.listen([&](ProcessExitEventArgs& e) {
        std::lock_guard<std::mutex> lock(mutex);
        ++exits;
        exitArgs = e;
    });
    check("Process start", process.start(child({"echo"})));
    const int code = process.waitForExit();
    check("Process waitForExit returns the exit code", code == 7 && process.getExitCode() == 7);
    check("Process not running after exit", !process.isRunning());
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool stderrSeen = false;
        std::string stdoutText;
        for (const auto& line : lines) {
            if (line.isStderr) stderrSeen = stderrSeen || line.line == "err1";
            else stdoutText += line.line + "|";
        }
        check("Process onLine stdout lines", stdoutText == "out1|a|b|c|");
        check("Process onLine stderr line", stderrSeen);
        check("Process onExit once, not killed", exits == 1 && exitArgs.exitCode == 7 && !exitArgs.killed);
    }

    // Restart after exit, refuse a second start while running, kill.
    std::atomic<bool> ready{false};
    auto readyListener = process.onLine.listen([&](ProcessLineEventArgs& e) {
        if (e.line == "ready") ready = true;
    });
    check("Process restarts", process.start(child({"sleep"})));
    check("second start while running is refused", !process.start(child({"echo"})));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check("sleeping child is ready", ready.load());
    process.kill();
    process.waitForExit();
    {
        std::lock_guard<std::mutex> lock(mutex);
        check("kill() ends the child", exits == 2 && exitArgs.killed && exitArgs.seconds < 10);
    }
}

void testDestructorKills() {
    const auto begin = std::chrono::steady_clock::now();
    {
        Process process;
        check("destructor test start", process.start(child({"sleep"})));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    check("destructor kills the child", elapsed < std::chrono::seconds(10));
}

void testDeliverMain() {
    Process process;
    std::vector<std::string> lines;  // main thread only
    bool exited = false;
    auto lineListener = process.onLine.listen([&](ProcessLineEventArgs& e) { lines.push_back(e.line); },
                                              Deliver::Main);
    auto exitListener = process.onExit.listen([&](ProcessExitEventArgs&) { exited = true; }, Deliver::Main);
    process.start(child({"echo"}));
    process.waitForExit();
    internal::drainMainThreadQueue();  // what the app loop does each frame
    check("Deliver::Main listeners run on drain", exited && lines.size() == 5);
}

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0) return runChildMode(argc - 2, argv + 2);

    std::error_code error;
    selfPath = fs::absolute(argv[0], error).string();
#if defined(_WIN32) || (defined(__APPLE__) && !TARGET_OS_IPHONE) || \
    (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
    isMainThread();  // pin the main thread before any worker runs
    testRunProcess();
    testProcess();
    testDestructorKills();
    testDeliverMain();
#else
    const auto result = runProcess({"anything"});
    check("unsupported platform reports not started", !result.started);
    std::printf("SKIP: processes are desktop only\n");
#endif
    std::printf("%s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
