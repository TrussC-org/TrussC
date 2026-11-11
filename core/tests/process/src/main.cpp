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
//     startAsync() while running is refused; kill() ends a sleeping child (and is
//     reported as killed); the destructor kills by default; default listeners
//     run on the worker; Deliver::Main runs on the main-thread queue drain.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/prctl.h>
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
    if (mode == "cap" || mode == "utf8cap") {
        const std::string line = mode == "cap" ? std::string(1024 * 1024, 'x') + "\n" :
            std::string(1024 * 1024 - 1, 'x') + "\xe3\x81\x82tail\n";
        std::fwrite(line.data(), 1, line.size(), stdout);
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
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (mode == "tree") {
        int readyPipe[2];
        if (pipe(readyPipe) != 0) return 98;
        const pid_t worker = fork();
        if (worker < 0) return 98;
        if (worker == 0) {
            close(readyPipe[0]);
            signal(SIGTERM, SIG_IGN);
            const char readyByte = 'r';
            if (write(readyPipe[1], &readyByte, 1) != 1) _exit(98);
            close(readyPipe[1]);
            for (;;) pause();
        }
        close(readyPipe[1]);
        char readyByte;
        if (read(readyPipe[0], &readyByte, 1) != 1) return 98;
        close(readyPipe[0]);
        std::printf("%ld\n", static_cast<long>(worker));
        std::fflush(stdout);
        for (;;) pause();  // default SIGTERM disposition: launcher dies first
    }
    if (mode == "stubborn") {
        signal(SIGTERM, SIG_IGN);
        std::puts("ready");
        std::fflush(stdout);
        for (;;) pause();
    }
#endif
    if (mode == "pid") {
#ifdef _WIN32
        std::printf("%lu\n", static_cast<unsigned long>(GetCurrentProcessId()));
#else
        std::printf("%ld\n", static_cast<long>(getpid()));
#endif
        std::fflush(stdout);
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::fprintf(stderr, "unknown child mode %s\n", mode.c_str());
    return 99;
}

// ---------------------------------------------------------------------------
// Parent side
// ---------------------------------------------------------------------------

// Must run before any TrussC thread helper or app initialization.
void testMainThreadIdentity() {
    const auto caller = std::this_thread::get_id();
    Process process;
    bool called = false, onCaller = false;
    auto listener = process.onExit.listen([&](ProcessExitEventArgs&) {
        called = true;
        onCaller = std::this_thread::get_id() == caller;
    }, Deliver::Main);
    check("start before app initialization", process.startAsync(child({"args"})));
    process.waitForExit();
    check("first Main callback waits for drain", !called);
    check("Process pins caller as main", getMainThreadId() == caller);
    internal::drainMainThreadQueue();
    check("first Main callback runs on caller", called && onCaller);
}

void testMainDeliveryLimit() {
    Process process;
    process.onLine.setMainThreadDeliveryLimit(2);
    process.onExit.setMainThreadDeliveryLimit(2);
    int lines = 0, exits = 0;
    auto line = process.onLine.listen([&](ProcessLineEventArgs&) { ++lines; }, Deliver::Main);
    auto exit = process.onExit.listen([&](ProcessExitEventArgs&) { ++exits; }, Deliver::Main);
    auto extraExit = process.onExit.listen([&](ProcessExitEventArgs&) { ++exits; }, Deliver::Main);
    auto droppedExit = process.onExit.listen([&](ProcessExitEventArgs&) { ++exits; }, Deliver::Main);
    for (int run = 0; run < 2; ++run) {
        check("limited output start", process.startAsync(child({"spam"})));
        check("limited output finishes", process.waitForExit() == 0);
        const auto store = internal::ExternalEventDelivery<ProcessLineEventArgs>::diagnostics(process.onLine);
        const auto exitStore = internal::ExternalEventDelivery<ProcessExitEventArgs>::diagnostics(process.onExit);
        check("child output respects limit before drain", store && store->pending == 2);
        check("exit copies share the per-event limit", exitStore && exitStore->pending == 2);
#ifndef __EMSCRIPTEN__
        // Four event copies plus one overflow warning per event.
        check("only limited copies and overflow warnings queued", internal::mainThreadQueue().size() == 6);
#endif
        check("limited callbacks wait for drain", lines == run * 2 && exits == run * 2);
        internal::drainMainThreadQueue();
        check("limited callbacks delivered and capacity released",
              lines == (run + 1) * 2 && exits == (run + 1) * 2 && store && store->pending == 0);
    }
}

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

    const std::string withNul("a\0b", 3);
    Process process;
    for (const auto& invalid : {std::vector<std::string>{withNul}, child({"args", withNul})}) {
        const auto rejected = runProcess(invalid);
        check("runProcess rejects NUL in program/argument", !rejected.started && rejected.exitCode == -1);
        check("startAsync rejects NUL in program/argument", !process.startAsync(invalid) && !process.isRunning());
    }
    const auto rejectedDir = runProcess(child({"echo"}), fs::path(withNul));
    check("runProcess rejects NUL in workingDir", !rejectedDir.started && rejectedDir.exitCode == -1);
    check("startAsync rejects NUL in workingDir", !process.startAsync(child({"echo"}), fs::path(withNul)));

    const auto capped = runProcess(child({"cap"}));
    check("exact line cap has no extra empty line", capped.exitCode == 0 && capped.output == std::string(1024 * 1024, 'x') + "\n");
    const auto utf8 = runProcess(child({"utf8cap"}));
    check("line cap preserves UTF-8 code points", utf8.exitCode == 0 && utf8.output == std::string(1024 * 1024 - 1, 'x') + "\xe3\x81\x82\ntail\n");

    const auto spam = runProcess(child({"spam"}));
    check("large output on both streams", spam.exitCode == 0 && spam.output.size() == 2 * 1024 * 1024);
}

void testProcess() {
    std::mutex mutex;
    std::vector<ProcessLineEventArgs> lines;
    int exits = 0;
    ProcessExitEventArgs exitArgs;
    const auto caller = std::this_thread::get_id();
    bool onWorker = true;

    Process process;
    auto lineListener = process.onLine.listen([&](ProcessLineEventArgs& e) {
        std::lock_guard<std::mutex> lock(mutex);
        onWorker = onWorker && std::this_thread::get_id() != caller;
        lines.push_back(e);
    });
    auto exitListener = process.onExit.listen([&](ProcessExitEventArgs& e) {
        std::lock_guard<std::mutex> lock(mutex);
        ++exits;
        exitArgs = e;
        onWorker = onWorker && std::this_thread::get_id() != caller;
    });
    check("Process start", process.startAsync(child({"echo"})));
    const int code = process.waitForExit();
    check("Process waitForExit returns the exit code", code == 7 && process.getExitCode() == 7);
    check("Process not running after exit", !process.isRunning());
    {
        std::lock_guard<std::mutex> lock(mutex);
        check("default callbacks finish on the worker before wait returns", onWorker && exits == 1);
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
    std::mutex readyMutex;
    std::condition_variable readyCondition;
    bool ready = false;
    auto readyListener = process.onLine.listen([&](ProcessLineEventArgs& e) {
        if (e.line == "ready") {
            { std::lock_guard<std::mutex> lock(readyMutex); ready = true; }
            readyCondition.notify_one();
        }
    });
    check("Process restarts", process.startAsync(child({"sleep"})));
    check("second start while running is refused", !process.startAsync(child({"echo"})));
    {
        std::unique_lock<std::mutex> lock(readyMutex);
        readyCondition.wait(lock, [&] { return ready; });  // outer harness bounds hangs
    }
    process.kill();
    const int killedCode = process.waitForExit();
    {
        std::lock_guard<std::mutex> lock(mutex);
        check("kill() ends the child", exits == 2 && exitArgs.killed && killedCode != 0);
    }
}

void testDestructorKills() {
    std::mutex mutex;
    std::condition_variable ready;
    unsigned long childPid = 0;
#ifdef _WIN32
    HANDLE childHandle = nullptr;
#endif
    {
        Process process;
        auto listener = process.onLine.listen([&](ProcessLineEventArgs& e) {
            { std::lock_guard<std::mutex> lock(mutex); childPid = std::stoul(e.line); }
            ready.notify_one();
        });
        check("destructor test start", process.startAsync(child({"pid"})));
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return childPid != 0; });
#ifdef _WIN32
        childHandle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(childPid));
#endif
    }
#ifdef _WIN32
    check("destructor kills the child", childHandle && WaitForSingleObject(childHandle, INFINITE) == WAIT_OBJECT_0);
    if (childHandle) CloseHandle(childHandle);
#else
    int status = 0;
    check("destructor kills the child", ::kill(static_cast<pid_t>(childPid), 0) == -1 && errno == ESRCH);
    check("destructor reaps the child", waitpid(static_cast<pid_t>(childPid), &status, WNOHANG) == -1 && errno == ECHILD);
#endif
}

void testDeliverMain() {
    Process process;
    std::vector<std::string> lines;
    bool exited = false;
    bool mainOnly = true;
    auto lineListener = process.onLine.listen([&](ProcessLineEventArgs& e) {
        mainOnly = mainOnly && isMainThread();
        lines.push_back(e.line);
    }, Deliver::Main);
    auto exitListener = process.onExit.listen([&](ProcessExitEventArgs&) {
        mainOnly = mainOnly && isMainThread();
        exited = true;
    }, Deliver::Main);
    check("Main delivery starts", process.startAsync(child({"echo"})));
    process.waitForExit();
    check("Main callbacks wait for drain", !exited && lines.empty());
    internal::drainMainThreadQueue();
    check("Deliver::Main runs on main", exited && lines.size() == 5 && mainOnly);

    bool staleCallback = false;
    EventListener listener;
    {
        Process temporary;
        listener = temporary.onExit.listen([&](ProcessExitEventArgs&) { staleCallback = true; }, Deliver::Main);
        temporary.startAsync(child({"echo"}));
        temporary.waitForExit();
    }
    internal::drainMainThreadQueue();
    check("destruction cancels queued callbacks", !staleCallback);
}

void testExitDestroysProcess() {
    auto process = std::make_unique<Process>();
    auto listener = process->onExit.listen([&](ProcessExitEventArgs&) { process.reset(); }, Deliver::Main);
    check("callback destruction start", process->startAsync(child({"echo"})));
    process->waitForExit();
    internal::drainMainThreadQueue();
    check("main onExit may destroy Process", !process);
}

// The thread-exit signal comes after the worker's post-callback instructions.
void testInlineDestroysProcess() {
    struct Completion {
        std::mutex mutex;
        std::condition_variable condition;
        bool ended = false;
    };
    struct SignalOnThreadExit {
        Completion* completion = nullptr;
        ~SignalOnThreadExit() {
            if (!completion) return;
            std::lock_guard<std::mutex> lock(completion->mutex);
            completion->ended = true;
            completion->condition.notify_one();
        }
    };
    for (bool destroyOnLine : {false, true}) {
        Completion completion;
        auto* process = new Process;
        auto destroy = [&](auto&) {
            thread_local SignalOnThreadExit afterWorker;
            afterWorker.completion = &completion;
            delete process;
        };
        EventListener listener;
        if (destroyOnLine) listener = process->onLine.listen(destroy, Deliver::Inline);
        else listener = process->onExit.listen(destroy, Deliver::Inline);
        check("inline callback destruction starts", process->startAsync(child({destroyOnLine ? "sleep" : "echo"})));
        std::unique_lock<std::mutex> lock(completion.mutex);
        completion.condition.wait(lock, [&] { return completion.ended; });
        check("inline callback may destroy Process and worker finishes", completion.ended);
    }
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
void testGroupCancellation() {
#if defined(__linux__)
    // Adopt the worker after its launcher exits, so death is proved by reaping.
    int previousSubreaper = 0;
    prctl(PR_GET_CHILD_SUBREAPER, &previousSubreaper);
    prctl(PR_SET_CHILD_SUBREAPER, 1);
#endif
    for (const char* mode : {"stubborn", "tree"}) {
        Process process;
        std::mutex mutex;
        std::condition_variable readyCondition;
        std::string readyLine;
        bool killed = false;
        auto lineListener = process.onLine.listen([&](ProcessLineEventArgs& e) {
            { std::lock_guard<std::mutex> lock(mutex); readyLine = e.line; }
            readyCondition.notify_one();
        });
        auto exitListener = process.onExit.listen([&](ProcessExitEventArgs& e) { killed = e.killed; }, Deliver::Inline);
        check("group cancellation starts", process.startAsync(child({mode})));
        {
            std::unique_lock<std::mutex> lock(mutex);
            readyCondition.wait(lock, [&] { return !readyLine.empty(); });
        }
        process.kill();
        const int code = process.waitForExit();
        check("group cancellation reports killed", killed);
        if (std::strcmp(mode, "stubborn") == 0) {
            check("stubborn immediate child killed", code == 128 + SIGKILL);
        } else {
            check("launcher exits on SIGTERM", code == 128 + SIGTERM);
            const pid_t workerPid = static_cast<pid_t>(std::stol(readyLine));
#if defined(__linux__)
            int status = 0;
            pid_t reaped;
            do { reaped = waitpid(workerPid, &status, 0); } while (reaped < 0 && errno == EINTR);
            check("stubborn descendant killed and reaped", reaped == workerPid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
#else
            // init reaps the orphan on macOS; the outer harness bounds this wait.
            while (::kill(workerPid, 0) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check("stubborn descendant gone", errno == ESRCH);
#endif
        }
    }
#if defined(__linux__)
    prctl(PR_SET_CHILD_SUBREAPER, previousSubreaper);
#endif
}
#endif

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0) return runChildMode(argc - 2, argv + 2);

    std::error_code error;
    selfPath = fs::absolute(argv[0], error).string();
#if defined(_WIN32) || (defined(__APPLE__) && !TARGET_OS_IPHONE) || \
    (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
    testMainThreadIdentity();
    testMainDeliveryLimit();
    testRunProcess();
    testProcess();
    testDestructorKills();
    testDeliverMain();
    testExitDestroysProcess();
    testInlineDestroysProcess();
#if !defined(_WIN32)
    testGroupCancellation();
#endif
#else
    const auto result = runProcess({"anything"});
    check("unsupported platform reports not started", !result.started);
    std::printf("SKIP: processes are desktop only\n");
#endif
    std::printf("%s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
