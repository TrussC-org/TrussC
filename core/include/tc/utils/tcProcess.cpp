// =============================================================================
// tcProcess.cpp - runProcess() / Process (see tcProcess.h)
//
// One ProcessRunner per run owns the OS resources: the child, its two output
// pipes and (Windows) its job object. run() reads both pipes until the child
// has exited, then drains only what is already buffered and closes them:
// a grandchild that inherited the pipes cannot keep run() waiting (same bound
// as internal/tcPosixBuildOutput.h and tcWindowsBuildOutput.h).
// =============================================================================

#include "tcProcess.h"
#include "tcLog.h"

#include <algorithm>
#include <cstring>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
    #define TC_PROCESS_WINDOWS 1
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
    #define TC_PROCESS_POSIX 1
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    #define TC_PROCESS_POSIX 1
#endif

#if defined(TC_PROCESS_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(TC_PROCESS_POSIX)
    #include <cerrno>
    #include <csignal>
    #include <fcntl.h>
    #include <poll.h>
    #include <spawn.h>
    #include <sys/ioctl.h>
    #include <sys/wait.h>
    #include <unistd.h>
    extern char** environ;
    #if defined(__APPLE__) || \
        (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29)))
        #define TC_PROCESS_SPAWN_CHDIR 1
    #endif
#endif

namespace trussc {
namespace internal {

// Splits a byte stream into lines at "\n", "\r\n" and a lone "\r".
class ProcessLineSplitter {
public:
    // Bound (child output): split lines at 1 MiB to bound each buffered line and event payload.
    static constexpr size_t maxLine = 1024 * 1024;

    template <class Emit>
    void feed(const char* data, size_t size, Emit&& emit) {
        for (size_t i = 0; i < size; ++i) {
            const char c = data[i];
            if (c == '\n') {
                if (lastWasCR_) { lastWasCR_ = false; continue; }  // second half of "\r\n"
                emitLine(emit);
            } else if (c == '\r') {
                emitLine(emit);
                lastWasCR_ = true;
            } else {
                lastWasCR_ = false;
                // Bound (UTF-8): allow at most three continuation bytes past maxLine to finish a code point.
                if (line_.size() >= maxLine &&
                    ((static_cast<unsigned char>(c) & 0xc0) != 0x80 || line_.size() >= maxLine + 3))
                    emitLine(emit);
                line_ += c;
            }
        }
    }

    template <class Emit>
    void flush(Emit&& emit) {
        if (!line_.empty()) emitLine(emit);
    }

private:
    template <class Emit>
    void emitLine(Emit& emit) {
        std::string line;
        line.swap(line_);
        emit(line);
    }

    std::string line_;
    bool lastWasCR_ = false;
};

#if defined(TC_PROCESS_WINDOWS)

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], size);
    return wide;
}

// Quotes one argument so CommandLineToArgvW / the MSVC CRT parse it back as-is.
void appendQuoted(std::wstring& commandLine, const std::wstring& arg) {
    if (!commandLine.empty()) commandLine += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        commandLine += arg;
        return;
    }
    commandLine += L'"';
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++backslashes; ++i; }
        if (i == arg.size()) {
            commandLine.append(backslashes * 2, L'\\');  // before the closing quote
            break;
        }
        if (arg[i] == L'"') {
            commandLine.append(backslashes * 2 + 1, L'\\');
        } else {
            commandLine.append(backslashes, L'\\');
        }
        commandLine += arg[i];
    }
    commandLine += L'"';
}

std::string windowsError(const char* what) {
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

} // namespace

class ProcessRunner {
public:
    ProcessRunner() = default;
    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    ~ProcessRunner() {
        for (HANDLE& pipe : reads_) close(pipe);
        if (process_ && !reaped_) {
            // run() never ran to the end: do not leave the program behind.
            if (job_) TerminateJobObject(job_, 1); else TerminateProcess(process_, 1);
            WaitForSingleObject(process_, INFINITE);
        }
        close(process_);
        close(job_);
    }

    bool spawn(const std::vector<std::string>& args, const fs::path& workingDir,
               std::string& error) {
        std::wstring commandLine;
        for (const auto& arg : args) appendQuoted(commandLine, widen(arg));

        SECURITY_ATTRIBUTES inheritable = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE writes[2] = {nullptr, nullptr};
        HANDLE nul = INVALID_HANDLE_VALUE;
        LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
        std::vector<char> attributeStorage;
        auto cleanup = [&] {
            for (HANDLE& pipe : writes) close(pipe);
            if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
            if (attributes) DeleteProcThreadAttributeList(attributes);
        };

        for (int i = 0; i < 2; ++i) {
            if (!CreatePipe(&reads_[i], &writes[i], &inheritable, 0) ||
                !SetHandleInformation(reads_[i], HANDLE_FLAG_INHERIT, 0)) {
                error = windowsError("CreatePipe");
                cleanup();
                return false;
            }
        }
        nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                          OPEN_EXISTING, 0, nullptr);
        if (nul == INVALID_HANDLE_VALUE) {
            error = windowsError("Opening NUL");
            cleanup();
            return false;
        }

        // Inherit only this child's three handles, not the pipes of another
        // Process started on another thread at the same time.
        HANDLE inherited[3] = {nul, writes[0], writes[1]};
        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
        attributeStorage.resize(attributeSize);
        attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize)) {
            attributes = nullptr;
            error = windowsError("InitializeProcThreadAttributeList");
            cleanup();
            return false;
        }
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                       sizeof(inherited), nullptr, nullptr)) {
            error = windowsError("UpdateProcThreadAttribute");
            cleanup();
            return false;
        }

        STARTUPINFOEXW startup = {};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nul;
        startup.StartupInfo.hStdOutput = writes[0];
        startup.StartupInfo.hStdError = writes[1];
        startup.lpAttributeList = attributes;

        // Suspended until it is in the job, so that nothing it starts escapes.
        const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW |
                            CREATE_UNICODE_ENVIRONMENT;
        PROCESS_INFORMATION info = {};
        const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                           flags, nullptr,
                                           workingDir.empty() ? nullptr : workingDir.c_str(),
                                           &startup.StartupInfo, &info);
        if (!created) {
            error = windowsError("CreateProcess");
            cleanup();
            return false;
        }
        cleanup();
        process_ = info.hProcess;
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ ||
            !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job_, process_)) {
            error = windowsError("Establishing process job ownership");
            TerminateProcess(process_, 1);  // child has never been resumed
            WaitForSingleObject(process_, INFINITE);
            reaped_ = true;
            CloseHandle(info.hThread);
            return false;
        }
        ResumeThread(info.hThread);
        CloseHandle(info.hThread);
        return true;
    }

    template <class Sink>
    int run(Sink&& sink, const std::atomic<bool>* killFlag, bool& killed) {
        bool finished = false;
        DWORD remaining[2] = {};
        while (!finished || reads_[0] || reads_[1]) {
            if (!finished && killFlag && !killed && killFlag->load(std::memory_order_acquire)) {
                killed = true;
                if (job_) TerminateJobObject(job_, 1); else TerminateProcess(process_, 1);
            }
            if (!finished && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
                finished = true;
                for (int i = 0; i < 2; ++i)
                    if (reads_[i]) PeekNamedPipe(reads_[i], nullptr, 0, nullptr, &remaining[i], nullptr);
            }
            bool received = false;
            for (int i = 0; i < 2; ++i) {
                if (!reads_[i]) continue;
                if (finished && !remaining[i]) { close(reads_[i]); continue; }
                DWORD available = 0;
                const bool open = PeekNamedPipe(reads_[i], nullptr, 0, nullptr, &available, nullptr) != 0;
                if (finished && available > remaining[i]) available = remaining[i];
                if (!open || (finished && !available)) { close(reads_[i]); continue; }
                if (!available) continue;
                char buffer[4096];
                DWORD count = 0;
                if (!ReadFile(reads_[i], buffer, (std::min)(available, DWORD(sizeof(buffer))), &count,
                              nullptr)) {
                    close(reads_[i]);
                    continue;
                }
                if (!count) continue;
                received = true;
                if (finished) remaining[i] -= count;
                sink(i, buffer, static_cast<size_t>(count));
            }
            // Idle: wait on the program, so its exit is seen at once.
            if (!received && !finished) WaitForSingleObject(process_, 10);
        }
        reaped_ = true;
        DWORD exitCode = 0;
        return GetExitCodeProcess(process_, &exitCode) ? static_cast<int>(exitCode) : -1;
    }

private:
    static void close(HANDLE& handle) {
        if (handle) CloseHandle(handle);
        handle = nullptr;
    }

    HANDLE reads_[2] = {nullptr, nullptr};
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    bool reaped_ = false;
};

#elif defined(TC_PROCESS_POSIX)

class ProcessRunner {
public:
    ProcessRunner() = default;
    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    ~ProcessRunner() {
        for (int& fd : reads_) close(fd);
        if (pid_ > 0 && !reaped_) {
            // run() never ran to the end: do not leave the program behind.
            ::kill(-pid_, SIGKILL);
            int status = 0;
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        }
    }

    bool spawn(const std::vector<std::string>& args, const fs::path& workingDir,
               std::string& error) {
#if !defined(TC_PROCESS_SPAWN_CHDIR)
        if (!workingDir.empty()) {
            error = "a working directory needs glibc 2.29 or newer";
            return false;
        }
#endif
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        int writes[2] = {-1, -1};
        auto fail = [&](const char* what, int code) {
            for (int& fd : writes) close(fd);
            for (int& fd : reads_) close(fd);
            error = std::string(what) + ": " + std::strerror(code);
            return false;
        };
        for (int i = 0; i < 2; ++i) {
            int fds[2];
            if (pipe(fds)) return fail("pipe", errno);
            reads_[i] = fds[0];
            writes[i] = fds[1];
        }
        // Keep the pipes off descriptors 0-2 (the app may run with one closed),
        // out of other children (CLOEXEC), and reads nonblocking.
        for (int* fd : {&reads_[0], &reads_[1], &writes[0], &writes[1]}) {
            if (*fd < 3) {
                const int moved = fcntl(*fd, F_DUPFD, 3);
                if (moved < 0) return fail("fcntl", errno);
                ::close(*fd);
                *fd = moved;
            }
            if (fcntl(*fd, F_SETFD, FD_CLOEXEC) < 0) return fail("fcntl", errno);
        }
        for (int fd : reads_)
            if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) return fail("fcntl", errno);

        struct Actions {
            posix_spawn_file_actions_t value;
            int error = posix_spawn_file_actions_init(&value);
            ~Actions() { if (!error) posix_spawn_file_actions_destroy(&value); }
        } actions;
        struct Attributes {
            posix_spawnattr_t value;
            int error = posix_spawnattr_init(&value);
            ~Attributes() { if (!error) posix_spawnattr_destroy(&value); }
        } attributes;
        if (actions.error) return fail("posix_spawn_file_actions_init", actions.error);
        if (attributes.error) return fail("posix_spawnattr_init", attributes.error);

        int code = posix_spawn_file_actions_addopen(&actions.value, 0, "/dev/null", O_RDONLY, 0);
        if (!code) code = posix_spawn_file_actions_adddup2(&actions.value, writes[0], 1);
        if (!code) code = posix_spawn_file_actions_adddup2(&actions.value, writes[1], 2);
#if defined(TC_PROCESS_SPAWN_CHDIR)
        if (!code && !workingDir.empty())
            code = posix_spawn_file_actions_addchdir_np(&actions.value, workingDir.c_str());
#endif
        if (code) return fail("posix_spawn_file_actions", code);

        // Own process group (kill() reaches its children), no inherited signal
        // mask, and default dispositions: an app that ignores SIGPIPE would
        // otherwise pass that on through exec.
        sigset_t noSignals, defaults;
        sigemptyset(&noSignals);
        sigfillset(&defaults);
        sigdelset(&defaults, SIGKILL);
        sigdelset(&defaults, SIGSTOP);
        code = posix_spawnattr_setflags(&attributes.value,
                                        POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK |
                                            POSIX_SPAWN_SETSIGDEF);
        if (!code) code = posix_spawnattr_setpgroup(&attributes.value, 0);
        if (!code) code = posix_spawnattr_setsigmask(&attributes.value, &noSignals);
        if (!code) code = posix_spawnattr_setsigdefault(&attributes.value, &defaults);
        if (code) return fail("posix_spawnattr", code);

        pid_t pid = 0;
        code = posix_spawnp(&pid, argv[0], &actions.value, &attributes.value, argv.data(), environ);
        for (int& fd : writes) close(fd);
        if (code) return fail("posix_spawnp", code);
        pid_ = pid;
        return true;
    }

    template <class Sink>
    int run(Sink&& sink, const std::atomic<bool>* killFlag, bool& killed) {
        using Clock = std::chrono::steady_clock;
        Clock::time_point terminatedAt;
        bool groupPending = false;
        bool finished = false;
        bool statusKnown = false;
        int status = 0;
        size_t remaining[2] = {};
        pollfd descriptors[2] = {{reads_[0], POLLIN, 0}, {reads_[1], POLLIN, 0}};
        while (!finished || descriptors[0].fd >= 0 || descriptors[1].fd >= 0 || groupPending) {
            if (killFlag && !killed && killFlag->load(std::memory_order_acquire)) {
                killed = true;
                groupPending = true;
                terminatedAt = Clock::now();
                ::kill(-pid_, SIGTERM);
            }
            // Reaping the leader does not end ownership of its group.
            if (groupPending) {
                if (::kill(-pid_, 0) < 0 && errno == ESRCH) groupPending = false;
                else if (Clock::now() - terminatedAt >= std::chrono::seconds(3)) {
                    ::kill(-pid_, SIGKILL);
                    groupPending = false;
                }
            }
            if (finished && groupPending) poll(nullptr, 0, 25);
            if (!finished) {
                pid_t waited;
                do { waited = waitpid(pid_, &status, WNOHANG); } while (waited < 0 && errno == EINTR);
                if (waited != 0) {
                    // waited < 0: already reaped elsewhere (e.g. SIGCHLD set
                    // to SIG_IGN); the program is gone but its status is lost.
                    finished = true;
                    reaped_ = true;
                    statusKnown = waited > 0;
                    for (int i = 0; i < 2; ++i) {
                        int available = 0;
                        if (descriptors[i].fd >= 0 &&
                            ioctl(descriptors[i].fd, FIONREAD, &available) == 0 && available > 0)
                            remaining[i] = static_cast<size_t>(available);
                    }
                } else {
                    // Also sleeps (25 ms) once both pipes are closed but the program runs on.
                    if (poll(descriptors, 2, 25) < 0 && errno != EINTR) {
                        for (auto& d : descriptors) d.fd = -1;
                    }
                }
            }
            for (int i = 0; i < 2; ++i) {
                auto& descriptor = descriptors[i];
                if (descriptor.fd < 0) continue;
                if (finished && !remaining[i]) { close(reads_[i]); descriptor.fd = -1; continue; }
                if (!finished && !descriptor.revents) continue;
                char bytes[4096];
                const size_t wanted = finished ? (std::min)(sizeof(bytes), remaining[i]) : sizeof(bytes);
                ssize_t count;
                do { count = read(descriptor.fd, bytes, wanted); } while (count < 0 && errno == EINTR);
                if (count > 0) {
                    if (finished) remaining[i] -= static_cast<size_t>(count);
                    sink(i, bytes, static_cast<size_t>(count));
                } else if (finished || count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    close(reads_[i]);
                    descriptor.fd = -1;
                }
            }
        }
        if (!statusKnown) return -1;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return -1;
    }

private:
    static void close(int& fd) {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }

    int reads_[2] = {-1, -1};
    pid_t pid_ = -1;
    bool reaped_ = false;
};

#else  // no processes on this platform

class ProcessRunner {
public:
    bool spawn(const std::vector<std::string>&, const fs::path&, std::string& error) {
        error = "running external processes is not supported on this platform";
        return false;
    }
    template <class Sink>
    int run(Sink&&, const std::atomic<bool>*, bool&) { return -1; }
};

#endif

namespace {

// Spawns, or logs why not. args must not be empty.
std::unique_ptr<ProcessRunner> spawnRunner(const std::vector<std::string>& args,
                                           const fs::path& workingDir) {
    if (args.empty() || args[0].empty()) {
        logError("Process") << "no program given";
        return nullptr;
    }
    const auto& directory = workingDir.native();
    if (std::any_of(args.begin(), args.end(), [](const auto& arg) {
            return arg.find('\0') != std::string::npos;
        }) || directory.find(fs::path::value_type{}) != fs::path::string_type::npos) {
        logError("Process") << "program, arguments and working directory must not contain NUL";
        return nullptr;
    }
    auto runner = std::make_unique<ProcessRunner>();
    std::string error;
    if (!runner->spawn(args, workingDir, error)) {
        logError("Process") << "could not start \"" << args[0] << "\": " << error;
        return nullptr;
    }
    return runner;
}

} // namespace
} // namespace internal

// -----------------------------------------------------------------------------
// runProcess
// -----------------------------------------------------------------------------

ProcessResult runProcess(const std::vector<std::string>& args, const fs::path& workingDir) {
    ProcessResult result;
    auto runner = internal::spawnRunner(args, workingDir);
    if (!runner) return result;
    result.started = true;

    internal::ProcessLineSplitter splitters[2];
    auto append = [&result](std::string& line) {
        result.output += line;
        result.output += '\n';
    };
    bool killed = false;
    result.exitCode = runner->run(
        [&](int stream, const char* data, size_t size) { splitters[stream].feed(data, size, append); },
        nullptr, killed);
    for (auto& splitter : splitters) splitter.flush(append);
    return result;
}

// -----------------------------------------------------------------------------
// Process
// -----------------------------------------------------------------------------

struct Process::State {
    Event<ProcessLineEventArgs> onLine;
    Event<ProcessExitEventArgs> onExit;
    std::unique_ptr<internal::ProcessRunner> runner;
    std::atomic<std::thread::id> workerId{};
    std::atomic<bool> running{false};
    std::atomic<bool> killRequested{false};
    std::atomic<int> exitCode{-1};
};

Process::Process() : state_(std::make_shared<State>()), onLine(state_->onLine), onExit(state_->onExit) {}

Process::~Process() {
    if (killOnDestroy_) kill();
    // Cancel queued callbacks and further emissions, including inline teardown.
    onLine.clear();
    onExit.clear();
    if (std::this_thread::get_id() == state_->workerId.load()) {
        // The worker owns State until it returns; it never accesses Process.
        std::lock_guard<std::mutex> lock(mutex_);
        worker_.detach();
        return;
    }
    joinWorker();
}

bool Process::startAsync(const std::vector<std::string>& args, const fs::path& workingDir) {
    getMainThreadId();  // Pin the caller before a worker's first Main delivery, even before runApp().
    if (std::this_thread::get_id() == state_->workerId.load()) {
        logError("Process") << "startAsync() from an inline onLine/onExit listener is not possible; "
                               "listen with Deliver::Main to restart";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_->running.load(std::memory_order_acquire)) {
        logError("Process") << "startAsync(): already running";
        return false;
    }
    if (worker_.joinable()) worker_.join();  // previous run, already finished
    state_->runner.reset();

    auto runner = internal::spawnRunner(args, workingDir);
    if (!runner) return false;
    state_->runner = std::move(runner);
    state_->killRequested.store(false, std::memory_order_release);
    state_->running.store(true, std::memory_order_release);

    const auto startedAt = std::chrono::steady_clock::now();
    worker_ = std::thread([state = state_, startedAt] {
        state->workerId.store(std::this_thread::get_id());
        internal::ProcessLineSplitter splitters[2];
        auto emitter = [state](bool isStderr) {
            return [state, isStderr](std::string& line) {
                ProcessLineEventArgs args;
                args.line = std::move(line);
                args.isStderr = isStderr;
                internal::ExternalEventDelivery<ProcessLineEventArgs>::notify(state->onLine, args);
            };
        };
        auto emitStdout = emitter(false);
        auto emitStderr = emitter(true);
        bool killed = false;
        const int code = state->runner->run(
            [&](int stream, const char* data, size_t size) {
                if (stream == 0) splitters[0].feed(data, size, emitStdout);
                else splitters[1].feed(data, size, emitStderr);
            },
            &state->killRequested, killed);
        splitters[0].flush(emitStdout);
        splitters[1].flush(emitStderr);

        state->exitCode.store(code, std::memory_order_release);
        state->running.store(false, std::memory_order_release);
        ProcessExitEventArgs exit;
        exit.exitCode = code;
        exit.seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
        exit.killed = killed;
        internal::ExternalEventDelivery<ProcessExitEventArgs>::notify(state->onExit, exit);
        state->workerId.store(std::thread::id());
    });
    return true;
}

bool Process::isRunning() const { return state_->running.load(std::memory_order_acquire); }
int Process::getExitCode() const { return state_->exitCode.load(std::memory_order_acquire); }

void Process::kill() {
    if (state_->running.load(std::memory_order_acquire)) state_->killRequested.store(true, std::memory_order_release);
}

int Process::waitForExit() {
    if (std::this_thread::get_id() == state_->workerId.load()) {
        logError("Process") << "waitForExit() from an inline onLine/onExit listener would wait "
                               "for itself";
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_->runner) return -1;
    }
    joinWorker();
    return state_->exitCode.load(std::memory_order_acquire);
}

void Process::joinWorker() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) worker_.join();
}

} // namespace trussc
