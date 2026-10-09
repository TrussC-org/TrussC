// Each fatal case execs a fresh copy; the parent checks both the OS status
// and diagnostics. No window or graphics context is needed.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>
#if TC_HAS_CRASH_HANDLER
#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

namespace {
#if TC_HAS_CRASH_HANDLER
using namespace tc;
std::string mode;
volatile uintptr_t badAddress = 0;

void fault() { *reinterpret_cast<volatile int*>(badAddress) = 1; }
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
int overflow(unsigned n) {
    volatile char reserve[8192];
    reserve[n % sizeof(reserve)] = static_cast<char>(n);
    // Runtime condition and work after recursion prevent tail-call removal.
    if (n == 0) return reserve[0];
    return overflow(n - 1) + reserve[n % sizeof(reserve)];
}
struct CrashApp : App {
    void setup() override {
        if (mode == "setup") throw std::runtime_error("crash-handler setup exception");
    }
    void update() override {
        internal::crashInput(SAPP_EVENTTYPE_MOUSE_DOWN, 42, 305, 0);
        throw std::runtime_error("crash-handler update exception");
    }
};
void priorTerminate() { std::_Exit(78); }
#ifndef _WIN32
void priorSignal(int, siginfo_t* info, void*) {
    const char line[] = "previous signal handler received original fault\n";
    if (info->si_addr == nullptr) {
        const auto written = write(STDERR_FILENO, line, sizeof(line) - 1);
        (void)written;
    }
    _exit(77);
}
void laterSignal(int) { _exit(79); }
#else
LONG WINAPI priorFilter(EXCEPTION_POINTERS*) { ExitProcess(77); }
LONG WINAPI laterFilter(EXCEPTION_POINTERS*) { ExitProcess(79); }
#endif

int child(const std::string& selected, const fs::path& directory) {
    mode = selected;
#ifdef _WIN32
    // Test harness only: avoid unattended CI prompts. The library does not
    // alter error mode, WER, or CRT dialog settings.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#else
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
    alarm(30); // deadlock watchdog, never a performance assertion
#endif
    if (mode == "prior-terminate") std::set_terminate(priorTerminate);
    if (mode == "prior-signal") {
#ifdef _WIN32
        SetUnhandledExceptionFilter(priorFilter);
#else
        struct sigaction action{};
        action.sa_sigaction = priorSignal;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, nullptr);
#endif
    }
    const auto log = directory / "crash.log";
    if (mode == "setup" || mode == "update") {
        // Headless apps do not read TRUSSC_LOG_FILE (#266); an explicit
        // setLogFile() still routes the crash report into the file. The
        // "setup" phase comes from setupNodeOnce(), shared with windowed Apps.
        if (!setLogFile(log)) return 90;
        return runHeadlessApp<CrashApp>();
    }
    internal::installCrashHandler();
    if (!setLogFile(log)) return 90;
    if (mode == "switch") {
        if (!setLogFile(directory / "new.log")) return 91;
    } else if (mode == "close") closeLogFile();
    else if (mode == "failed-open") {
        if (setLogFile(log / "not-a-directory")) return 92;
    } else if (mode == "local-logger") {
        Logger local;
        if (!local.setLogFile(directory / "local.log")) return 93;
    }
    if (mode == "later-terminate") std::set_terminate(priorTerminate);
    if (mode == "later-signal") {
#ifdef _WIN32
        SetUnhandledExceptionFilter(laterFilter);
#else
        signal(SIGSEGV, laterSignal);
#endif
        internal::installCrashHandler(); // must not reclaim a later hook
        fault();
    }
    internal::CrashPhaseScope phase("update");
    for (int i = 0; i < 12; ++i) {
        internal::crashFrame(800 + i);
        internal::crashInput(SAPP_EVENTTYPE_MOUSE_DOWN, i, 305, 0);
    }
    internal::crashFrame(812);
    if (mode == "throw") throw std::runtime_error("crash-handler test exception");
    if (mode == "non-std") throw 42;
    if (mode == "terminate" || mode == "prior-terminate" || mode == "later-terminate") std::terminate();
    if (mode == "overflow") return overflow(1000000);
    if (mode == "worker") { std::thread worker(fault); worker.join(); return 94; }
    if (mode == "event") {
        auto listener = events().rawEvent.listen([](const sapp_event&) { fault(); });
        sapp_event event{};
        event.type = SAPP_EVENTTYPE_KEY_DOWN;
        event.key_code = SAPP_KEYCODE_A;
        // Sokol advances this counter after _frame_cb; the crash context
        // still names the current main-loop frame (812).
        event.frame_count = 813;
        internal::_event_cb(&event);
        return 95;
    }
    if (mode == "abort") std::abort();
#ifndef _WIN32
    if (mode == "bus") raise(SIGBUS);
    else if (mode == "fpe") raise(SIGFPE);
    else if (mode == "ill") raise(SIGILL);
    else fault();
#else
    fault();
#endif
    return 96;
}

std::string read(const fs::path& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int launch(const char* executable, const char* selected, const fs::path& directory) {
#ifdef _WIN32
    const std::wstring args = L"\"" + fs::path(executable).wstring() + L"\" "
#ifdef TC_CORE_TEST_NAME
        L"crashHandler "
#endif
        L"--child " + fs::path(selected).wstring() + L" \"" + directory.wstring() + L"\"";
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE err = CreateFileW((directory / "stderr.log").c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdError = err;
    startup.hStdOutput = err;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> command(args.begin(), args.end());
    command.push_back(0);
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                       nullptr, &startup, &process);
    CloseHandle(err);
    if (!started) return -1;
    const DWORD result = WaitForSingleObject(process.hProcess, 30000);
    if (result != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 99); WaitForSingleObject(process.hProcess, INFINITE); }
    DWORD status = 0;
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result == WAIT_OBJECT_0 ? static_cast<int>(status) : -1;
#else
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (!pid) {
        const int err = open((directory / "stderr.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (err < 0 || dup2(err, STDERR_FILENO) < 0) _exit(97);
        close(err);
        execl(executable, executable,
#ifdef TC_CORE_TEST_NAME
              "crashHandler",
#endif
              "--child", selected, directory.c_str(), static_cast<char*>(nullptr));
        _exit(98);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) { if (errno != EINTR) return -1; }
    return status;
#endif
}
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if TC_HAS_CRASH_HANDLER
    if (argc == 4 && std::string(argv[1]) == "--child") return child(argv[2], argv[3]);
    const fs::path root = fs::current_path() / ("crashHandler-test-" + std::to_string(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
    ));
    int failures = 0;
    auto check = [&](bool okay, const std::string& message) {
        std::printf("%s %s\n", okay ? "PASS" : "FAIL", message.c_str());
        if (!okay) ++failures;
    };
    for (const char* selected : {"throw", "non-std", "terminate", "abort", "segv", "worker", "event",
                                 "switch", "close", "failed-open", "local-logger", "prior-signal",
                                 "later-signal", "prior-terminate", "later-terminate", "setup", "update"
#ifndef _WIN32
                                 , "bus", "fpe", "ill", "overflow"
#endif
         }) {
        const std::string name(selected);
        const auto directory = root / name;
        fs::create_directories(directory);
        const int status = launch(fs::absolute(argv[0]).string().c_str(), selected, directory);
        const bool overridden = name == "later-signal" || name == "later-terminate";
        int expectedExit = name == "prior-signal" ? 77 : name == "later-signal" ? 79 :
                           name == "prior-terminate" || name == "later-terminate" ? 78 : 0;
#ifdef _WIN32
        const bool aborts = name == "throw" || name == "non-std" || name == "terminate" || name == "abort" ||
                            name == "setup" || name == "update";
        check(expectedExit ? status == expectedExit : aborts ? status == 3 : static_cast<unsigned>(status) == EXCEPTION_ACCESS_VIOLATION,
              name + ": fatal exit / previous handler status");
#else
        int expectedSignal = name == "throw" || name == "non-std" || name == "terminate" || name == "abort" ||
                             name == "setup" || name == "update" ? SIGABRT :
                             name == "bus" ? SIGBUS : name == "fpe" ? SIGFPE : name == "ill" ? SIGILL : SIGSEGV;
        check(status != -1 && (expectedExit ? WIFEXITED(status) && WEXITSTATUS(status) == expectedExit :
                              WIFSIGNALED(status) && WTERMSIG(status) == expectedSignal),
              name + ": original signal / previous handler status");
#endif
        const std::string err = read(directory / "stderr.log");
        const std::string log = read(directory / (name == "switch" ? "new.log" : "crash.log"));
        const std::string marker = "*** TrussC crash:";
        check((err.find(marker) != std::string::npos) == !overridden, name + ": stderr report / later hook precedence");
        check((log.find(marker) != std::string::npos) == (!overridden && name != "close"), name + ": log report");
        if (!overridden) {
            check(err.find(marker, err.find(marker) + marker.size()) == std::string::npos, name + ": one report");
            check(err.find("#0 0x") != std::string::npos && err.find(" (base 0x") != std::string::npos,
                  name + ": raw PC and module base/offset");
            if (name != "setup" && name != "update") {
                check(err.find("main-loop frame 812") != std::string::npos, name + ": frame context");
                check(err.find("; frame 800\n") == std::string::npos && err.find("; frame 811\n") != std::string::npos,
                      name + ": ring retains newest inputs");
            }
        }
        if (name == "throw" || name == "setup" || name == "update")
            check(err.find("what(): crash-handler ") != std::string::npos, name + ": exception what()");
        if (name == "throw") {
#ifdef __APPLE__
            const std::string hint = "Resolve raw addresses";
#else
            const std::string hint = "Resolve module offsets";
#endif
            for (const auto* sink : {&err, &log}) {
                const auto first = sink->find(hint);
                check(first != std::string::npos && sink->find(hint, first + hint.size()) == std::string::npos,
                      name + (sink == &err ? ": stderr" : ": log") + " resolver hint exactly once");
            }
        }
        if (name == "terminate") check(err.find("no active exception") != std::string::npos, name + ": empty exception");
        if (name == "non-std") check(err.find("non-std exception") != std::string::npos, name + ": unknown exception");
        if (name == "event") {
            check(err.find("phase: event dispatch") != std::string::npos, name + ": event phase");
            for (const auto* sink : {&err, &log})
                check(sink->find("main-loop frame 812 / phase: event dispatch") != std::string::npos &&
                      sink->find("keyPressed (0, 0) 65; frame 812\n") != std::string::npos &&
                      sink->find("; frame 813\n") == std::string::npos,
                      name + (sink == &err ? ": stderr" : ": log") + " input matches header, not advanced event counter");
        }
        if (name == "update") {
            for (const auto* sink : {&err, &log})
                check(sink->find("main-loop frame 0 / phase: update") != std::string::npos &&
                      sink->find("mousePressed (42, 305) 0; frame 0\n") != std::string::npos,
                      name + (sink == &err ? ": stderr" : ": log") + " input matches headless header frame");
        }
        if (name == "setup" || name == "update") check(err.find("phase: " + name) != std::string::npos, name + ": runtime phase");
        if (name == "switch") check(read(directory / "crash.log").find(marker) == std::string::npos, name + ": retired log untouched");
#ifndef _WIN32
        if (name == "prior-signal") check(err.find("received original fault") != std::string::npos, name + ": original siginfo");
#endif
    }
    if (!failures) fs::remove_all(root);
    else std::printf("Diagnostics retained in %s\n", root.string().c_str());
    return failures ? 1 : 0;
#else
    (void)argc; (void)argv;
    std::puts("SKIP: crash handler is desktop-only");
    return 0;
#endif
}
