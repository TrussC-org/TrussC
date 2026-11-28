#include "ProcessCapture.h"
#include "OperationLog.h"
#include "WindowsCommand.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include "BuildSetup.h"   // quoteWindowsArgument
#include <windows.h>
#include <cwchar>
#include <io.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace std;

static void spawnDiagnostic(const string& message) {
    if (projectOps::diagnosticCallback) {
        projectOps::diagnostic(message, projectOps::MessageKind::Diagnostic);
    } else cerr << message << "\n";
}

namespace projectOps {
struct ProcessRunner {
    static void state(CancelHandle* handle, ProcessState value) {
        if (handle) handle->state_ = value;
    }
};

#ifdef _WIN32
static string resolveWindowsProgram(const string& program) {
    if (program.find_first_of("/\\:") != string::npos) return program;
    // The CRT spawn search checks the current directory first, then each
    // PATH directory with all extensions before moving to the next directory.
    vector<string> directories{"."};
    if (const char* environment = getenv("PATH")) {
        const string path = environment;
        size_t start = 0;
        do {
            const size_t end = path.find(';', start);
            string directory = path.substr(start, end - start);
            if (directory.size() >= 2 && directory.front() == '"' && directory.back() == '"')
                directory = directory.substr(1, directory.size() - 2);
            if (!directory.empty()) directories.push_back(directory);
            if (end == string::npos) break;
            start = end + 1;
        } while (start < path.size());
    }
    const vector<string> extensions = program.find('.') == string::npos
        ? vector<string>{".com", ".exe", ".bat", ".cmd"} : vector<string>{""};
    for (const auto& directory : directories) {
        for (const auto& extension : extensions) {
            const string candidate = directory + "\\" + program + extension;
            const DWORD attributes = GetFileAttributesA(candidate.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const DWORD size = GetFullPathNameA(candidate.c_str(), 0, nullptr, nullptr);
            if (!size) continue;
            vector<char> path(size);
            const DWORD length = GetFullPathNameA(candidate.c_str(), size, path.data(), nullptr);
            if (length && length < size) return string(path.data(), length);
        }
    }
    return program;
}

static void requestChildCtrlBreak(DWORD processId) {
    // Only the internal helper attaches to the child's console. In particular,
    // never attach/detach the caller around an asynchronous console event.
    vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) return;
    wstring command = L"\"" + wstring(executable.data(), length) + L"\" --internal-ctrl-break " + to_wstring(processId);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION helper = {};
    if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS, nullptr, nullptr, &startup, &helper)) return;
    WaitForSingleObject(helper.hProcess, 1000);
    CloseHandle(helper.hThread);
    CloseHandle(helper.hProcess);
}

struct ChildWindows {
    DWORD processId;
    bool found = false;
};
static BOOL CALLBACK closeChildWindow(HWND window, LPARAM context) {
    auto& child = *reinterpret_cast<ChildWindows*>(context);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != child.processId || !IsWindowVisible(window)) return TRUE;
    // The child's console window also reports the child's PID; closing it
    // would end every process attached to that console.
    wchar_t className[32] = {};
    GetClassNameW(window, className, 32);
    if (wcscmp(className, L"ConsoleWindowClass") == 0 || wcscmp(className, L"PseudoConsoleWindow") == 0)
        return TRUE;
    child.found = true;
    PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}
#endif

ProcessResult runProcess(const vector<string>& argv,
    const function<void(const char*, size_t)>& onData, const ProcessOptions& options) {
    ProcessResult result;
    auto* handle = options.cancel;
    const bool ownGroup = handle != nullptr;
    if (handle && handle->stopRequested()) { result.stopped = true; return result; }
    if (argv.empty()) return result;
    // Preserve the ordering of existing CLI output and inherited child output.
    cout.flush();
    cerr.flush();
#ifndef _WIN32
    vector<char*> cargv;
    for (const auto& argument : argv) cargv.push_back(const_cast<char*>(argument.c_str()));
    cargv.push_back(nullptr);
    int fds[2] = {-1, -1};
    if (!options.inheritOutput) {
        if (pipe(fds) != 0) return result;
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    int error = 0;
    if (!options.workingDirectory.empty()) {
#ifdef __APPLE__
        // Keep the call available to deployment targets older than macOS 26.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        error = posix_spawn_file_actions_addchdir_np(&actions, options.workingDirectory.c_str());
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif
    }
    if (!options.inheritOutput) {
        if (!error) error = posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
        if (!error) error = posix_spawn_file_actions_adddup2(&actions,
            options.stderrFile ? fileno(options.stderrFile) : fds[1], 2);
    }
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    if (ownGroup) {
        posix_spawnattr_setpgroup(&attributes, 0);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    }
    pid_t pid = 0;
    if (!error) error = posix_spawnp(&pid, cargv[0], &actions, &attributes, cargv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (fds[1] >= 0) close(fds[1]);
    if (error) {
        if (fds[0] >= 0) close(fds[0]);
        spawnDiagnostic("Error: failed to spawn '" + argv[0] + "': " + strerror(error));
        return result;
    }
    auto sendSignal = [&](int signalNumber) { kill(ownGroup ? -pid : pid, signalNumber); };
#else
    string commandLine = options.windowsCommandLine;
    string application;
    if (commandLine.empty()) {
        application = resolveWindowsProgram(argv[0]);
        commandLine = quoteWindowsArgument(application);
        for (size_t i = 1; i < argv.size(); ++i) {
            if (!commandLine.empty()) commandLine += ' ';
            commandLine += quoteWindowsArgument(argv[i]);
        }
        string extension = application.substr(application.find_last_of('.') + 1);
        for (char& c : extension) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (extension == "bat" || extension == "cmd") {
            application = resolveWindowsProgram("cmd.exe");
            commandLine = windowsShellCommandLine(commandLine, application);
        }
    }
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!options.inheritOutput) {
        if (!CreatePipe(&readEnd, &writeEnd, &security, 0)) return result;
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE job = ownGroup ? CreateJobObjectW(nullptr, nullptr) : nullptr;
    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    if (ownGroup && options.hideConsole && GetConsoleCP() == 0) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = options.inheritOutput ? GetStdHandle(STD_OUTPUT_HANDLE) : writeEnd;
    startup.hStdError = options.inheritOutput ? GetStdHandle(STD_ERROR_HANDLE) : writeEnd;
    if (options.stderrFile) {
        startup.hStdError = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(options.stderrFile)));
        SetHandleInformation(startup.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    // A no-console caller needs a child console for CTRL_BREAK. Known console
    // tools request a hidden one; GUI apps retain their normal show state.
    DWORD flags = CREATE_SUSPENDED;
    // CLI children retain the console group so Ctrl-C reaches their own
    // handlers exactly as before; GUI/layer children get an isolated group.
    if (ownGroup) flags |= CREATE_NEW_PROCESS_GROUP;
    if (GetConsoleCP() == 0) flags |= ownGroup ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
    PROCESS_INFORMATION process = {};
    const BOOL started = (!ownGroup || job) && CreateProcessA(application.empty() ? nullptr : application.c_str(),
        commandLine.data(), nullptr, nullptr, TRUE, flags, nullptr,
        options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str(), &startup, &process);
    if (writeEnd) CloseHandle(writeEnd);
    if (!started || (job && !AssignProcessToJobObject(job, process.hProcess))) {
        if (started) { TerminateProcess(process.hProcess, 1); CloseHandle(process.hProcess); CloseHandle(process.hThread); }
        if (readEnd) CloseHandle(readEnd);
        if (job) CloseHandle(job);
        if (!options.inheritOutput) spawnDiagnostic("Error: failed to spawn '" + argv[0] + "'");
        return result;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
#endif
    ProcessRunner::state(handle, ProcessState::Running);
    bool stopSent = false, forceSent = false;
    chrono::steady_clock::time_point requestedAt;
    auto checkStop = [&]() {
        if (handle && handle->stopRequested() && !stopSent) {
            stopSent = true;
            result.stopped = true;
            requestedAt = chrono::steady_clock::now();
            ProcessRunner::state(handle, ProcessState::StopRequested);
#ifndef _WIN32
            sendSignal(SIGTERM);
#else
            // Rule: visible non-console top-level windows get WM_CLOSE only;
            // otherwise the child's console group gets CTRL_BREAK.
            ChildWindows windows{process.dwProcessId};
            EnumWindows(closeChildWindow, reinterpret_cast<LPARAM>(&windows));
            if (!windows.found) requestChildCtrlBreak(process.dwProcessId);
#endif
        }
        const bool outputLimit = options.outputLimitExceeded && *options.outputLimitExceeded;
        if (((handle && handle->forceRequested()) || outputLimit) && !forceSent) {
            forceSent = true;
            result.stopped = true;
#ifndef _WIN32
            sendSignal(SIGKILL);
#else
            if (job) TerminateJobObject(job, 1);
            else TerminateProcess(process.hProcess, 1);
#endif
        }
        if (stopSent && chrono::steady_clock::now() - requestedAt >= stopGracePeriod)
            ProcessRunner::state(handle, ProcessState::StillRunning);
    };
    char buffer[4096];
    bool exited = false, pipeClosed = options.inheritOutput;
#ifndef _WIN32
    int status = 0;
    while (true) {
        checkStop();
        pollfd descriptor = {pipeClosed ? -1 : fds[0], POLLIN, 0};
        const bool treeRunning = stopSent && ownGroup && kill(-pid, 0) == 0;
        const int ready = poll(&descriptor, 1, exited && !treeRunning ? 0 : 50);
        if (ready < 0 && errno == EINTR) continue;
        if (ready > 0) {
            const ssize_t count = read(fds[0], buffer, sizeof(buffer));
            if (count > 0) {
                if (onData) onData(buffer, static_cast<size_t>(count));
            } else if (count == 0) pipeClosed = true;
        }
        if (exited && !treeRunning && (ready <= 0 || pipeClosed)) break;
        if (!exited) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) exited = true;
            else if (waited < 0 && errno != EINTR) break;
        }
    }
    if (fds[0] >= 0) close(fds[0]);
    result.exitCode = exited && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
    while (true) {
        checkStop();
        DWORD available = 0;
        if (!pipeClosed && !PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) pipeClosed = true;
        if (available) {
            DWORD count = 0;
            const DWORD wanted = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
            if (ReadFile(readEnd, buffer, wanted, &count, nullptr) && count && onData) onData(buffer, count);
        }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
        const bool treeRunning = stopSent && job &&
            QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                &accounting, sizeof(accounting), nullptr) && accounting.ActiveProcesses > 0;
        if (exited && !treeRunning && (!available || pipeClosed)) break;
        if (!exited) exited = WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
        if (!available && (!exited || treeRunning)) Sleep(2);
    }
    if (readEnd) CloseHandle(readEnd);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(process.hProcess, &exitCode)) result.exitCode = static_cast<int>(exitCode);
    CloseHandle(process.hProcess);
    if (job) CloseHandle(job);
#endif
    ProcessRunner::state(handle, ProcessState::Exited);
    return result;
}
} // namespace projectOps

#ifdef _WIN32
static BOOL WINAPI ignoreHelperSignal(DWORD) { return TRUE; }

// Private trusscli dispatch only; deliberately absent from the public header.
int trusscliInternalCtrlBreak(const char* argument) {
    DWORD processId = 0;
    if (!*argument) return 1;
    for (const char* digit = argument; *digit; ++digit) {
        if (*digit < '0' || *digit > '9') return 1;
        const DWORD value = static_cast<DWORD>(*digit - '0');
        if (processId > (MAXDWORD - value) / 10) return 1;
        processId = processId * 10 + value;
    }
    if (!processId || processId == ATTACH_PARENT_PROCESS || !AttachConsole(processId)) return 1;
    if (!SetConsoleCtrlHandler(ignoreHelperSignal, TRUE)) return 1;
    // Stay attached until process exit, so a delayed event cannot reach the
    // caller. The group ID is always the launched child's PID, never zero.
    return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, processId) ? 0 : 1;
}
#endif

int runCaptured(const vector<string>& argv, const function<void(const char*, size_t)>& onData,
                FILE* stderrFile, const bool* cancel) {
    projectOps::ProcessOptions options;
    options.stderrFile = stderrFile;
    options.outputLimitExceeded = cancel;
    return projectOps::runProcess(argv, onData, options).exitCode;
}

int runInherited(const vector<string>& argv) {
    projectOps::ProcessOptions options;
    options.inheritOutput = true;
    return projectOps::runProcess(argv, {}, options).exitCode;
}
