#pragma once

#ifdef _WIN32
#include <windows.h>
#include <string>
#include "tcBuildOutputCapture.h"
#include <new>

namespace trussc::internal {

// Keep each stream's original destination and bounded diagnostics from
// both. Drain both pipes so either can exceed its pipe
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
    BuildOutputCapture captured[2];
    bool finished = false;
    DWORD remaining[2] = {};
    while (reads[0] || reads[1]) {
        if (!finished && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            finished = true;
            // Bound (security): after direct-child exit, drain only each pipe's readable snapshot, then close; surviving writers cannot extend the wait.
            for (int i = 0; i < 2; ++i)
                if (reads[i]) PeekNamedPipe(reads[i], nullptr, 0, nullptr, &remaining[i], nullptr);
        }
        bool received = false;
        for (int i = 0; i < 2; ++i) {
            if (!reads[i]) continue;
            if (finished && !remaining[i]) {
                CloseHandle(reads[i]); reads[i] = nullptr; continue;
            }
            DWORD available = 0;
            // Fixed read chunk; capture overflow is handled and counted by BuildOutputCapture.
            char buffer[4096];
            DWORD count = 0;
            const bool open = PeekNamedPipe(reads[i], nullptr, 0, nullptr, &available, nullptr) != 0;
            if (finished && available > remaining[i]) available = remaining[i];
            if (!open || (finished && !available) || (available &&
                !ReadFile(reads[i], buffer, available < sizeof(buffer) ? available : sizeof(buffer),
                          &count, nullptr))) {
                CloseHandle(reads[i]);
                reads[i] = nullptr;
                continue;
            }
            if (!count) continue;
            received = true;
            captured[i].append(buffer, count);
            if (finished) remaining[i] -= count;
        }
        if (!received && (reads[0] || reads[1])) Sleep(1);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);
    buildOutputLoss().record(captured[0].omittedBytes() + captured[1].omittedBytes());
    // Defer forwarding until the child has exited: a blocked destination must
    // never prevent us from draining either child pipe.
#if defined(__cpp_exceptions)
    try {
#endif
        bool forwardingFailed = false;
        for (int stream = 0; stream < 2; ++stream) {
            const auto text = captured[stream].text();
            if (stream && !output.empty() && output.back() != '\n') output += '\n';
            output += text;
            size_t offset = 0;
            DWORD written = 0;
            while (offset < text.size() && WriteFile(destinations[stream], text.data() + offset,
                    static_cast<DWORD>(text.size() - offset), &written, nullptr) && written)
                offset += written;
            if (offset != text.size()) forwardingFailed = true;
        }
        if (forwardingFailed) buildOutputLoss().recordForwardingFailure();
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        output.clear();
        try { buildOutputLoss().recordStorageFailure(); } catch (...) {}
    }
#endif
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return gotExitCode ? static_cast<int>(exitCode) : -1;
}

} // namespace trussc::internal
#endif
