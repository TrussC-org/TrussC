#pragma once

#ifdef _WIN32
#include <windows.h>
#include <string>

namespace trussc::internal {

// Keep each stream's original destination while retaining complete lines from
// both for failure diagnostics. Drain both pipes so either can exceed its pipe
// buffer without blocking the child. The caller supplies a quoted argv command
// line; no shell is involved.
inline int runWindowsBuildWithOutput(std::string commandLine, std::string& output,
                                     DWORD creationFlags = 0) {
    output.clear();
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reads[2] = {nullptr, nullptr};
    HANDLE writes[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        if (!CreatePipe(&reads[i], &writes[i], &security, 0) ||
            !SetHandleInformation(reads[i], HANDLE_FLAG_INHERIT, 0)) {
            for (int j = 0; j < 2; ++j) {
                if (reads[j]) CloseHandle(reads[j]);
                if (writes[j]) CloseHandle(writes[j]);
            }
            return -1;
        }
    }
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writes[0];
    si.hStdError = writes[1];
    PROCESS_INFORMATION pi = {};
    const BOOL started = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr,
                                        TRUE, creationFlags, nullptr, nullptr, &si, &pi);
    for (HANDLE pipe : writes) CloseHandle(pipe);
    if (!started) {
        for (HANDLE pipe : reads) CloseHandle(pipe);
        return -1;
    }
    const HANDLE destinations[2] = {GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
    std::string pending[2];
    while (reads[0] || reads[1]) {
        bool received = false;
        for (int i = 0; i < 2; ++i) {
            if (!reads[i]) continue;
            DWORD available = 0;
            // Bound (security/latency): child-process output uses finite per-read storage. Overflow continues in the next read.
            char buffer[4096];
            DWORD count = 0;
            const bool open = PeekNamedPipe(reads[i], nullptr, 0, nullptr, &available, nullptr) != 0;
            if (!open || (available &&
                !ReadFile(reads[i], buffer, available < sizeof(buffer) ? available : sizeof(buffer),
                          &count, nullptr))) {
                // Separate unterminated fragments so stdout and stderr cannot
                // accidentally form a diagnostic together.
                if (!pending[i].empty()) output.append(pending[i]).push_back('\n');
                CloseHandle(reads[i]);
                reads[i] = nullptr;
                continue;
            }
            if (!count) continue;
            received = true;
            pending[i].append(buffer, count);
            const auto end = pending[i].rfind('\n');
            if (end != std::string::npos) {
                output.append(pending[i], 0, end + 1);
                pending[i].erase(0, end + 1);
            }
            DWORD offset = 0;
            DWORD written = 0;
            while (offset < count && WriteFile(destinations[i], buffer + offset, count - offset,
                                               &written, nullptr) && written != 0) {
                offset += written;
            }
        }
        if (!received && (reads[0] || reads[1])) Sleep(1);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return gotExitCode ? static_cast<int>(exitCode) : -1;
}

} // namespace trussc::internal
#endif
