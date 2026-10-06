#include "tcCrashHandler.h"

#if TC_HAS_CRASH_HANDLER
#include "../../sokol/sokol_app_tc.h"
#include <exception>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach/vm_prot.h>
#if defined(__aarch64__)
#include <mach/arm/thread_status.h>
#endif
#include <mach-o/loader.h>
#else
#include <link.h>
#endif
#endif

namespace trussc::internal {
namespace {
CrashContext contextState;
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<const char*>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

// The handler never touches the loader, the Logger, a mutex, a stream, or
// the heap. Metadata is collected at startup (and after loading a guest).
// A writer may retire an old descriptor/snapshot only before reporting has
// begun. Sequentially consistent publication + the one-way reporting flag
// keep a crashing thread's snapshot alive without a lock or reference count.
struct Module { uintptr_t begin, end, base; std::string name; };
using Modules = std::vector<Module>;
std::atomic<Modules*> modules{nullptr};
std::mutex moduleMutex;
std::atomic<bool> reporting{false};
std::once_flag installed;
std::terminate_handler previousTerminate = nullptr;
char output[16384];
size_t used = 0;
#ifdef _WIN32
std::atomic<HANDLE> logFile{INVALID_HANDLE_VALUE};
HANDLE stderrHandle = INVALID_HANDLE_VALUE;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
std::atomic<DWORD> cppReportThread{0};
using AbortHandler = void (*)(int);
AbortHandler previousAbort = SIG_DFL;
#else
std::atomic<int> logFile{-1};
constexpr int fatalSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
struct sigaction previousSignals[5];
alignas(16) unsigned char alternateStack[128 * 1024];
uintptr_t mainStackLow = 0, mainStackHigh = 0;
#endif

void text(const char* s) noexcept {
    if (!s) return;
    while (*s && used < sizeof(output) - 1) output[used++] = *s++;
}
void number(uint64_t n, unsigned base = 10) noexcept {
    char digits[32];
    unsigned count = 0;
    do { digits[count++] = "0123456789abcdef"[n % base]; n /= base; } while (n);
    while (count && used < sizeof(output) - 1) output[used++] = digits[--count];
}
void signedNumber(int n) noexcept {
    if (n < 0) { text("-"); number(static_cast<uint64_t>(-static_cast<int64_t>(n))); }
    else number(static_cast<unsigned>(n));
}
void hex(uintptr_t n) noexcept { text("0x"); number(n, 16); }

const char* inputName(int type) noexcept {
    switch (type) {
        case SAPP_EVENTTYPE_KEY_DOWN: return "keyPressed";
        case SAPP_EVENTTYPE_KEY_UP: return "keyReleased";
        case SAPP_EVENTTYPE_CHAR: return "character";
        case SAPP_EVENTTYPE_MOUSE_DOWN: return "mousePressed";
        case SAPP_EVENTTYPE_MOUSE_UP: return "mouseReleased";
        case SAPP_EVENTTYPE_MOUSE_SCROLL: return "mouseScrolled";
        case SAPP_EVENTTYPE_MOUSE_MOVE: return "mouseMoved";
        case SAPP_EVENTTYPE_MOUSE_ENTER: return "mouseEntered";
        case SAPP_EVENTTYPE_MOUSE_LEAVE: return "mouseExited";
        default: return "touch";
    }
}
void context() noexcept {
    text("\n    main-loop frame "); number(contextState.frame.load(std::memory_order_relaxed));
    text(" / phase: "); text(contextState.phase.load(std::memory_order_relaxed));
    text("\n    recent input (type, x, y, key/button; frame):\n");
    const unsigned count = contextState.inputCount.load();
    const unsigned length = count < 8 ? count : 8;
    for (unsigned i = 0; i < length; ++i) {
        const unsigned n = count - length + i;
        const auto& e = contextState.inputs[n % 8];
        const unsigned sequence = e.sequence.load();
        if (sequence != n * 2 + 2) continue; // interrupted or overwritten slot
        const int type = e.type.load(std::memory_order_relaxed);
        const int x = e.x.load(std::memory_order_relaxed);
        const int y = e.y.load(std::memory_order_relaxed);
        const int detail = e.detail.load(std::memory_order_relaxed);
        const uint64_t frame = e.frame.load(std::memory_order_relaxed);
        if (e.sequence.load() != sequence) continue;
        text("      "); text(inputName(type)); text(" ("); signedNumber(x);
        text(", "); signedNumber(y); text(") "); signedNumber(detail);
        text("; frame "); number(frame); text("\n");
    }
    text("    backtrace (best effort; frame pointers may be omitted):\n");
}
void address(unsigned index, uintptr_t pc) noexcept {
    text("      #"); number(index); text(" "); hex(pc);
    const auto* snapshot = modules.load();
    if (snapshot) {
        for (const auto& m : *snapshot) {
            if (pc < m.begin || pc >= m.end) continue;
            text(" "); text(m.name.c_str()); text(" + "); hex(pc - m.base);
            text(" (base "); hex(m.base); text(")");
            break;
        }
    }
    text("\n");
}
#ifdef _WIN32
void writeTo(HANDLE handle) noexcept {
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    DWORD done = 0;
    size_t offset = 0;
    while (offset < used && WriteFile(handle, output + offset,
           static_cast<DWORD>(used - offset), &done, nullptr) && done) offset += done;
}
#else
void writeTo(int fd) noexcept {
    if (fd < 0) return;
    size_t offset = 0;
    while (offset < used) {
        const ssize_t n = write(fd, output + offset, used - offset);
        if (n > 0) offset += static_cast<size_t>(n);
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
}
#endif
void flush() noexcept {
#ifdef _WIN32
    text("    Resolve module offsets with the matching PDB offline.\n");
    writeTo(stderrHandle);
#elif defined(__APPLE__)
    text("    Resolve raw addresses with atos -l <base> -o <module> <address>.\n");
    writeTo(STDERR_FILENO);
#else
    text("    Resolve module offsets with addr2line -C -f -e <module> <offset>.\n");
    writeTo(STDERR_FILENO);
#endif
    writeTo(logFile.load());
}

#ifndef _WIN32
// Walk only validated frame records on the startup/main thread's stack.
// backtrace()/dladdr()/backtrace_symbols_fd() are NOT specified to be
// async-signal-safe, even after warming up. Do not call them here. A worker
// crash still gets its fault PC; an omitted/corrupt frame pointer ends the
// walk. No process-wide compiler flag is needed for these diagnostics.
void stack(uintptr_t pc, uintptr_t fp, uintptr_t sp) noexcept {
    if (pc) address(0, pc);
    if (sp < mainStackLow || sp >= mainStackHigh) return;
    for (unsigned i = 1; i < 64; ++i) {
        if (fp < sp || fp < mainStackLow || fp > mainStackHigh - 2 * sizeof(uintptr_t)
            || fp % alignof(uintptr_t)) break;
        const auto* frame = reinterpret_cast<const uintptr_t*>(fp);
        const uintptr_t next = frame[0], caller = frame[1];
        if (!caller) break;
        address(i, caller);
        if (next <= fp) break;
        sp = fp + 2 * sizeof(uintptr_t);
        fp = next;
    }
}
void signalStack(void* raw) noexcept {
    const auto* c = static_cast<const ucontext_t*>(raw);
#if defined(__APPLE__) && defined(__x86_64__)
    stack(c->uc_mcontext->__ss.__rip, c->uc_mcontext->__ss.__rbp, c->uc_mcontext->__ss.__rsp);
#elif defined(__APPLE__) && defined(__aarch64__)
    stack(arm_thread_state64_get_pc(c->uc_mcontext->__ss),
          arm_thread_state64_get_fp(c->uc_mcontext->__ss),
          arm_thread_state64_get_sp(c->uc_mcontext->__ss));
#elif defined(__x86_64__)
    stack(c->uc_mcontext.gregs[REG_RIP], c->uc_mcontext.gregs[REG_RBP], c->uc_mcontext.gregs[REG_RSP]);
#elif defined(__aarch64__)
    stack(c->uc_mcontext.pc, c->uc_mcontext.regs[29], c->uc_mcontext.sp);
#else
    (void)c;
    text("      fault-context unwinding unavailable on this architecture\n");
#endif
}
const char* signalName(int sig) noexcept {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        case SIGABRT: return "SIGABRT";
        default: return "signal";
    }
}
void onSignal(int sig, siginfo_t* info, void* raw) noexcept {
    const int savedErrno = errno;
    if (!reporting.exchange(true)) {
        text("\n*** TrussC crash: "); text(signalName(sig));
        if (info->si_code > 0) {
            text(" (address "); hex(reinterpret_cast<uintptr_t>(info->si_addr)); text(")");
        }
        context();
        signalStack(raw);
        flush();
    }
    // Restore the previous disposition. Custom SA_SIGINFO handlers receive
    // the ORIGINAL fault context (a raise() would replace it with SI_TKILL).
    // Default dispositions are re-raised on this same thread; the signal is
    // pending until we return, retaining signal status and OS core reports.
    for (unsigned i = 0; i < 5; ++i) {
        if (fatalSignals[i] != sig) continue;
        const auto& old = previousSignals[i];
        sigaction(sig, &old, nullptr);
        if ((old.sa_flags & SA_RESETHAND) && old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
            struct sigaction reset{};
            reset.sa_handler = SIG_DFL;
            sigemptyset(&reset.sa_mask);
            sigaction(sig, &reset, nullptr);
        }
        errno = savedErrno;
        if (old.sa_handler == SIG_DFL) raise(sig);
        else if (old.sa_handler != SIG_IGN) {
            if (old.sa_flags & SA_SIGINFO) old.sa_sigaction(sig, info, raw);
            else old.sa_handler(sig);
        }
        return;
    }
}
#else
void windowsStack(const CONTEXT* fault = nullptr) noexcept {
    // CaptureStackBackTrace uses the OS unwinder, without DbgHelp's symbol
    // locks/heap. Symbol resolution is deliberately left to offline tools.
    // Include the original fault PC before the handler's own call stack.
    unsigned first = 0;
    if (fault) {
#if defined(_M_X64) || defined(__x86_64__)
        address(first++, fault->Rip);
#elif defined(_M_ARM64)
        address(first++, fault->Pc);
#elif defined(_M_IX86)
        address(first++, fault->Eip);
#endif
    }
    void* frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) address(first + i, reinterpret_cast<uintptr_t>(frames[i]));
}
LONG WINAPI onException(EXCEPTION_POINTERS* exception) {
    if (!reporting.exchange(true)) {
        // Report before chaining even if an earlier SDK exits from its filter.
        // MSVC's CRT may subsequently call terminate for a C++ exception;
        // that hook appends what() without starting a second crash block.
        if (exception->ExceptionRecord->ExceptionCode == 0xe06d7363)
            cppReportThread.store(GetCurrentThreadId());
        text("\n*** TrussC crash: Windows exception ");
        hex(exception->ExceptionRecord->ExceptionCode);
        text(" (address "); hex(reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress)); text(")");
        const auto* record = exception->ExceptionRecord;
        if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
            && record->NumberParameters >= 2) {
            text(" (fault address "); hex(record->ExceptionInformation[1]); text(")");
        }
        context(); windowsStack(exception->ContextRecord); flush();
    }
    if (previousFilter) return previousFilter(exception);
    return EXCEPTION_CONTINUE_SEARCH;
}
void onAbort(int sig) {
    if (!reporting.exchange(true)) {
        text("\n*** TrussC crash: SIGABRT"); context(); windowsStack(); flush();
    }
    signal(SIGABRT, previousAbort);
    if (previousAbort != SIG_DFL && previousAbort != SIG_IGN) previousAbort(sig);
    // Returning lets UCRT abort continue its normal fatal path.
}
#endif

void onTerminate() noexcept {
    const bool first = !reporting.exchange(true);
    bool supplement = false;
#ifdef _WIN32
    DWORD thread = GetCurrentThreadId();
    supplement = !first && cppReportThread.compare_exchange_strong(thread, 0);
#endif
    if (first || supplement) {
        if (supplement) used = 0;
        text(first ? "\n*** TrussC crash: std::terminate" : "\n    std::terminate");
        // This is the C++ terminate path, never the POSIX signal/SEH path.
        // The runtime owns the exception; what() is copied into our buffer.
        if (auto exception = std::current_exception()) {
            try { std::rethrow_exception(exception); }
            catch (const std::exception& e) { text(" / what(): "); text(e.what()); }
            catch (...) { text(" / non-std exception"); }
        } else text(" / no active exception");
        if (first) {
            context();
#ifdef _WIN32
            windowsStack();
#else
            const uintptr_t fp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
            stack(reinterpret_cast<uintptr_t>(&onTerminate), fp, fp);
#endif
        } else text("\n");
        flush();
    }
    if (previousTerminate) previousTerminate();
    std::abort(); // a broken custom terminate handler must not return
}
} // namespace

CrashContext& crashContext() { return contextState; }

void refreshCrashModules() noexcept try {
    std::lock_guard<std::mutex> guard(moduleMutex);
    auto next = std::make_unique<Modules>();
#ifdef _WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W m{};
        m.dwSize = sizeof(m);
        if (Module32FirstW(snapshot, &m)) do {
            const auto base = reinterpret_cast<uintptr_t>(m.modBaseAddr);
            const auto utf8 = std::filesystem::path(m.szExePath).u8string();
            next->push_back({base, base + m.modBaseSize, base,
                             std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size())});
        } while (Module32NextW(snapshot, &m));
        CloseHandle(snapshot);
    }
#elif defined(__APPLE__)
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const auto* h = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (h->magic != MH_MAGIC_64) continue;
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const auto base = reinterpret_cast<uintptr_t>(h);
        const auto* command = reinterpret_cast<const load_command*>(h + 1);
        for (uint32_t j = 0; j < h->ncmds; ++j) {
            if (command->cmd == LC_SEGMENT_64) {
                const auto* segment = reinterpret_cast<const segment_command_64*>(command);
                if (segment->initprot & VM_PROT_EXECUTE) {
                    const uintptr_t begin = segment->vmaddr + slide;
                    next->push_back({begin, begin + segment->vmsize, base, _dyld_get_image_name(i)});
                }
            }
            command = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(command) + command->cmdsize);
        }
    }
#else
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) {
        std::string name = info->dlpi_name;
        if (name.empty()) {
            char path[4096];
            const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
            if (n > 0) name.assign(path, static_cast<size_t>(n));
            else name = "<executable>";
        }
        for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
            const auto& p = info->dlpi_phdr[i];
            if (p.p_type != PT_LOAD || !(p.p_flags & PF_X)) continue;
            const uintptr_t begin = info->dlpi_addr + p.p_vaddr;
            static_cast<Modules*>(data)->push_back({begin, begin + p.p_memsz, info->dlpi_addr, name});
        }
        return 0;
    }, next.get());
#endif
    auto* old = modules.exchange(next.release());
    if (!reporting.load()) delete old;
} catch (...) {
    // Keep the last snapshot if diagnostics cannot allocate during a reload.
}

void closeCrashLogFile() {
#ifdef _WIN32
    const HANDLE old = logFile.exchange(INVALID_HANDLE_VALUE);
    if (!reporting.load() && old != INVALID_HANDLE_VALUE) CloseHandle(old);
#else
    const int old = logFile.exchange(-1);
    if (!reporting.load() && old >= 0) close(old);
#endif
}
void setCrashLogFile(const std::filesystem::path& path) {
#ifdef _WIN32
    HANDLE next = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE old = logFile.exchange(next);
    if (!reporting.load() && old != INVALID_HANDLE_VALUE) CloseHandle(old);
#else
    const int next = open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
    const int old = logFile.exchange(next);
    if (!reporting.load() && old >= 0) close(old);
#endif
}
void installCrashHandler() {
    std::call_once(installed, [] {
        refreshCrashModules();
#ifdef _WIN32
        stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
        previousFilter = SetUnhandledExceptionFilter(onException);
        previousAbort = signal(SIGABRT, onAbort);
#else
#ifdef __APPLE__
        mainStackHigh = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
        mainStackLow = mainStackHigh - pthread_get_stacksize_np(pthread_self());
#else
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) == 0) {
            void* base = nullptr;
            size_t size = 0;
            if (pthread_attr_getstack(&attr, &base, &size) == 0) {
                mainStackLow = reinterpret_cast<uintptr_t>(base);
                mainStackHigh = mainStackLow + size;
            }
            pthread_attr_destroy(&attr);
        }
#endif
        // sigaltstack is per-thread. Preserve an SDK's existing alternate
        // stack; install ours only on the thread starting the app.
        stack_t oldStack{};
        if (sigaltstack(nullptr, &oldStack) == 0 && (oldStack.ss_flags & SS_DISABLE)) {
            stack_t alt{};
            alt.ss_sp = alternateStack;
            alt.ss_size = sizeof(alternateStack);
            sigaltstack(&alt, nullptr);
        }
        struct sigaction action{};
        action.sa_sigaction = onSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        for (unsigned i = 0; i < 5; ++i) {
            sigaction(fatalSignals[i], nullptr, &previousSignals[i]);
            action.sa_mask = previousSignals[i].sa_mask;
            action.sa_flags = SA_SIGINFO | SA_ONSTACK | (previousSignals[i].sa_flags & SA_NODEFER);
            sigaction(fatalSignals[i], &action, nullptr);
        }
#endif
        previousTerminate = std::set_terminate(onTerminate);
    });
}
} // namespace trussc::internal
#endif
