#pragma once

// Test-only #239 diagnostics. No Logger, heap-owned state, or teardown hooks.
#ifdef TC_SESSION_TRACE
#include <cstdio>
#include <cstdint>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <chrono>
#include <ctime>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#else
#include <pthread.h>
#endif
#endif

namespace trussc::internal {
inline void sessionTrace(const char* label, unsigned long long context = 0) noexcept {
    char line[768];
#ifdef _WIN32
    struct Sink { HANDLE file; LARGE_INTEGER start, frequency; };
    // One instance across core/app TUs. Trivial static state and an intentionally
    // leaked handle remain valid in atexit handlers and static destructors.
    static const Sink sink = [] {
        Sink result{};
        QueryPerformanceCounter(&result.start);
        QueryPerformanceFrequency(&result.frequency);
        result.file = CreateFileW(L"C:\\Users\\Public\\device-239-session-trace.log",
            FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return result;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    SYSTEMTIME wall{};
    GetSystemTime(&wall);
    const double elapsed = sink.frequency.QuadPart
        ? 1000.0 * double(now.QuadPart - sink.start.QuadPart) / double(sink.frequency.QuadPart) : 0.0;
    const int count = std::snprintf(line, sizeof(line),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ +%.3fms pid=%lu tid=%lu context=0x%llx %s\n",
        unsigned(wall.wYear), unsigned(wall.wMonth), unsigned(wall.wDay),
        unsigned(wall.wHour), unsigned(wall.wMinute), unsigned(wall.wSecond), unsigned(wall.wMilliseconds),
        elapsed, GetCurrentProcessId(), GetCurrentThreadId(), context, label);
#else
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const auto wall = std::chrono::system_clock::now();
    const auto stamp = std::chrono::system_clock::to_time_t(wall);
    std::tm utc{};
    gmtime_r(&stamp, &utc);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count() % 1000;
#ifdef __linux__
    const auto threadId = static_cast<unsigned long long>(syscall(SYS_gettid));
#elif defined(__APPLE__)
    uint64_t threadId = 0;
    pthread_threadid_np(nullptr, &threadId);
#else
    const auto threadId = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
    const int count = std::snprintf(line, sizeof(line),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ +%.3fms pid=%ld tid=%llu context=0x%llx %s\n",
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
        static_cast<long long>(millis), elapsed, static_cast<long>(getpid()),
        static_cast<unsigned long long>(threadId), context, label);
#endif
    if (count <= 0) return;
    const auto bytes = static_cast<unsigned int>(count < static_cast<int>(sizeof(line)) ? count : sizeof(line) - 1);
    line[bytes - 1] = '\n';
    line[bytes] = '\0';
#ifdef _WIN32
    if (sink.file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        // A single append per line; no diagnostic mutex can block the watchdog.
        WriteFile(sink.file, line, bytes, &written, nullptr);
        FlushFileBuffers(sink.file);
    }
    OutputDebugStringA(line);
#else
    std::fwrite(line, 1, bytes, stderr);
    std::fflush(stderr);
#endif
}
} // namespace trussc::internal
#define TC_STRACE(...) ::trussc::internal::sessionTrace(__VA_ARGS__)
#else
#define TC_STRACE(...) ((void)0)
#endif
