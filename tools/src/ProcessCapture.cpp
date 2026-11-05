#include "ProcessCapture.h"

#include <cerrno>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include "BuildSetup.h"   // quoteWindowsArgument
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace std;

#ifdef _WIN32

int runCaptured(const vector<string>& argv, const function<void(const char*, size_t)>& onData) {
    if (argv.empty()) return -1;
    // Text printed so far must come before the child's output, also through a pipe.
    cout.flush();
    cerr.flush();
    string commandLine;
    for (const auto& a : argv) {
        if (!commandLine.empty()) commandLine += ' ';
        commandLine += quoteWindowsArgument(a);
    }

    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &security, 0)) return -1;
    // The read end stays with us; the child inherits only the write end.
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writeEnd;   // stdout and stderr share one pipe: output stays in order
    si.hStdError = writeEnd;
    PROCESS_INFORMATION pi = {};
    // Without a console (GUI launch), keep the child hidden as before.
    const DWORD flags = GetConsoleCP() != 0 ? 0 : CREATE_NO_WINDOW;
    const BOOL started = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        flags, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd);
    if (!started) {
        CloseHandle(readEnd);
        cerr << "Error: failed to spawn '" << argv[0] << "'\n";
        return -1;
    }

    char buffer[4096];
    bool exited = false;
    while (true) {
        DWORD available = 0;
        // A broken pipe means every writer has closed.
        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available) {
            DWORD count = 0;
            const DWORD wanted = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
            if (!ReadFile(readEnd, buffer, wanted, &count, nullptr) || count == 0) break;
            onData(buffer, count);
            continue;
        }
        if (exited) break;   // the child is gone and the pipe is drained
        // Re-check the pipe once after seeing the exit: the last bytes were
        // written before the child ended.
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) exited = true;
        else Sleep(2);
    }
    CloseHandle(readEnd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return gotExitCode ? static_cast<int>(exitCode) : -1;
}

#else

int runCaptured(const vector<string>& argv, const function<void(const char*, size_t)>& onData) {
    if (argv.empty()) return -1;
    // Text printed so far must come before the child's output, also through a pipe.
    cout.flush();
    cerr.flush();
    vector<char*> cargv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return -1;
    // Close-on-exec keeps both ends out of the child; dup2 below clears it
    // on the copies that become the child's stdout and stderr.
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // stdout and stderr share one pipe: output stays in order
    int error = posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
    pid_t pid = 0;
    if (!error) error = posix_spawnp(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (error) {
        close(fds[0]);
        cerr << "Error: failed to spawn '" << argv[0] << "': " << strerror(error) << "\n";
        return -1;
    }

    int status = 0;
    bool exited = false;
    char buffer[4096];
    while (true) {
        pollfd p = {fds[0], POLLIN, 0};
        // Once the child is gone, only read what is already in the pipe.
        const int ready = poll(&p, 1, exited ? 0 : 50);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready > 0) {
            const ssize_t count = read(fds[0], buffer, sizeof(buffer));
            if (count > 0) { onData(buffer, static_cast<size_t>(count)); continue; }
            if (count == 0) break;                       // every writer closed
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        if (exited) break;
        pid_t waited;
        do { waited = waitpid(pid, &status, WNOHANG); } while (waited < 0 && errno == EINTR);
        if (waited != 0) exited = true;   // one more pass reads the last bytes
    }
    close(fds[0]);
    if (!exited) {
        pid_t waited;
        do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited < 0) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#endif
