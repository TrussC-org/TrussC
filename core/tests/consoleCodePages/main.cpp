// Exercise the production Windows console paths with deterministic API mocks.
// These tests cover control flow, not conhost/CRT encoding or Windows ABI.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
using UINT = unsigned;
using DWORD = unsigned;
using BOOL = int;
using LONG = long;
#define WINAPI
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr UINT CP_UTF8 = 65001;
constexpr DWORD CTRL_C_EVENT = 0, CTRL_BREAK_EVENT = 1, CTRL_CLOSE_EVENT = 2;
constexpr DWORD ATTACH_PARENT_PROCESS = static_cast<DWORD>(-1);
constexpr DWORD EXCEPTION_ACCESS_VIOLATION = 0xc0000005;
constexpr DWORD EXCEPTION_IN_PAGE_ERROR = 0xc0000006;
constexpr LONG EXCEPTION_CONTINUE_SEARCH = 0;

int failures = 0;
void check(bool okay, const char* message) {
    std::printf("%s: %s\n", okay ? "PASS" : "FAIL", message);
    if (!okay) ++failures;
}
UINT outputPage = 437, inputPage = 932;
bool failOutput = false, failInput = false, failCtrl = false;
int outputWrites = 0, inputWrites = 0;
using CtrlHandler = BOOL (*)(DWORD);
CtrlHandler ctrlHandler = nullptr;
UINT GetConsoleOutputCP() { return outputPage; }
UINT GetConsoleCP() { return inputPage; }
BOOL SetConsoleOutputCP(UINT value) {
    ++outputWrites;
    if (failOutput || value == 0) return FALSE;
    outputPage = value;
    return TRUE;
}
BOOL SetConsoleCP(UINT value) {
    ++inputWrites;
    if (failInput || value == 0) return FALSE;
    inputPage = value;
    return TRUE;
}
BOOL SetConsoleCtrlHandler(CtrlHandler handler, BOOL add) {
    if (failCtrl) return FALSE;
    ctrlHandler = add ? handler : nullptr;
    return TRUE;
}
BOOL AttachConsole(DWORD) { return FALSE; }
BOOL AllocConsole() { return FALSE; }
int freopen_s(FILE**, const char*, const char*, FILE*) { return 1; }

struct sapp_desc {
    struct { bool console_create, console_attach, console_utf8; } win32{};
};
struct {
    struct {
        sapp_desc desc;
#include "sokol_state.inc"
    } app;
} _sapp_tc{};
#include "sokol_console.inc"

// Minimal exception-report dependencies. Check that the real filter restores
// after flushing the report and before handing off to an earlier filter.
struct CONTEXT {};
struct MockExceptionRecord {
    DWORD ExceptionCode;
    void* ExceptionAddress;
    DWORD NumberParameters;
    uintptr_t ExceptionInformation[2];
};
struct EXCEPTION_POINTERS { MockExceptionRecord* ExceptionRecord; CONTEXT* ContextRecord; };
std::atomic<bool> reporting{false};
std::atomic<DWORD> cppReportThread{0};
LONG (*previousFilter)(EXCEPTION_POINTERS*) = nullptr;
DWORD GetCurrentThreadId() { return 1; }
void text(const char*) {}
void hex(uintptr_t) {}
void context() {}
void windowsStack(const CONTEXT*) {}
void flush() {
    check(outputPage == CP_UTF8 && inputPage == CP_UTF8,
          "crash report is written before code pages are restored");
}
#include "crash_state.inc"
#include "crash_setter.inc"
#include "crash_exception.inc"
#include "console_guard.inc"

void reset(bool outputFailure = false, bool inputFailure = false) {
    _sapp_tc = {};
    _sapp_tc.app.desc.win32.console_utf8 = true;
    outputPage = 437;
    inputPage = 932;
    failOutput = outputFailure;
    failInput = inputFailure;
    outputWrites = inputWrites = 0;
}
LONG priorFilter(EXCEPTION_POINTERS*) {
    check(outputPage == 437 && inputPage == 932,
          "previous crash filter sees both original code pages");
    return 77;
}
void verifyExit() {
    check(outputPage == 437 && inputPage == 932, "std::exit restores both code pages");
    check(outputWrites == 1 && inputWrites == 1,
          "repeated guards register only one active exit callback");
    std::fflush(stdout);
    if (failures) std::_Exit(1);
}
} // namespace

int main() {
    // Registered first, so this observes the real atexit callback's result.
    if (std::atexit(verifyExit) != 0) return 1;
    for (bool outputFailure : {false, true}) {
        for (bool inputFailure : {false, true}) {
            reset(outputFailure, inputFailure);
            _sapp_tc_win32_init_console();
            check(outputPage == (outputFailure ? 437u : CP_UTF8) &&
                  inputPage == (inputFailure ? 932u : CP_UTF8),
                  "each UTF-8 switch succeeds or fails independently");
            // Make changes after initialization so an erroneous restore of a
            // failed switch is observable even when its original value matched.
            outputPage = inputPage = 850;
            failOutput = failInput = false;
            _sapp_tc_win32_restore_console();
            check(outputPage == (outputFailure ? 850u : 437u) &&
                  inputPage == (inputFailure ? 850u : 932u),
                  "only successful switches restore their own original values");
        }
    }
    reset();
    _sapp_tc.app.desc.win32.console_utf8 = false;
    _sapp_tc_win32_init_console();
    _sapp_tc_win32_restore_console();
    check(outputWrites == 0 && inputWrites == 0, "disabled option leaves console untouched");

    for (DWORD event : {CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT}) {
        reset();
        {
            ConsoleCPCtrlGuard guard;
            _sapp_tc_win32_init_console();
            check(ctrlHandler && ctrlHandler(event) == FALSE,
                  "console control callback preserves default termination");
            check(outputPage == 437 && inputPage == 932, "control event restores both values");
            _sapp_tc_win32_restore_console();
        }
        check(ctrlHandler == nullptr, "guard unregisters control handler");
        outputPage = inputPage = 850;
        restoreConsoleCP();
        check(outputPage == 850 && inputPage == 850 && crashConsoleCodePages.load() == 0,
              "normal exit disarms exit and crash fallbacks");
    }
    reset();
    {
        ConsoleCPCtrlGuard guard;
        _sapp_tc_win32_init_console();
        check(ctrlHandler(6) == FALSE && outputPage == CP_UTF8 && inputPage == CP_UTF8,
              "unhandled control event leaves code pages alone");
        MockExceptionRecord record{EXCEPTION_ACCESS_VIOLATION, nullptr, 0, {}};
        CONTEXT contextRecord;
        EXCEPTION_POINTERS exception{&record, &contextRecord};
        previousFilter = priorFilter;
        check(onException(&exception) == 77, "crash keeps previous filter result");
        // Already reporting must still restore before chaining.
        outputPage = inputPage = CP_UTF8;
        check(onException(&exception) == 77, "repeated crash still restores");
        previousFilter = nullptr;
        check(onException(&exception) == EXCEPTION_CONTINUE_SEARCH,
              "crash without previous filter continues exception search");
        _sapp_tc_win32_restore_console();
    }
    reset(true, true);
    outputPage = inputPage = 0;
    {
        ConsoleCPCtrlGuard guard;
        _sapp_tc_win32_init_console();
        _sapp_tc_win32_restore_console();
        outputWrites = inputWrites = 0;
        restoreConsoleCP();
        EXCEPTION_POINTERS exception{}; // reporting is already true
        onException(&exception);
        check(!ctrlHandler && outputWrites == 0 && inputWrites == 0,
              "no console means no control handler or fallback writes");
    }

    reset();
    failCtrl = true;
    ConsoleCPCtrlGuard guard;
    _sapp_tc_win32_init_console();
    check(!ctrlHandler, "control registration failure does not prevent exit restoration");
    outputWrites = inputWrites = 0;
    std::exit(failures ? 1 : 0); // deliberately skip the guard destructor
}
