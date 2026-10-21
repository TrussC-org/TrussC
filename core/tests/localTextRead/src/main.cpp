#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <chrono>
#include <fstream>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
#ifdef __linux__
#include <unistd.h>
#endif
#if defined(TC_LOCAL_TEXT_WRAP_NEW) || defined(_WIN32)
namespace {
thread_local size_t failAllocationAt = 0;
thread_local bool failEveryAllocation = false;
thread_local size_t largestAllocation = 0;
thread_local size_t injectedRefusals = 0;
void recordAllocation(size_t size) {
    largestAllocation = std::max(largestAllocation, size);
    if (failAllocationAt && size >= failAllocationAt) {
        ++injectedRefusals;
        if (!failEveryAllocation) failAllocationAt = 0;
        throw std::bad_alloc();
    }
}
}
#ifdef TC_LOCAL_TEXT_WRAP_NEW
extern "C" void* __real__Znwm(size_t);
extern "C" void* __wrap__Znwm(size_t size) {
    recordAllocation(size);
    return __real__Znwm(size);
}
#else
// This test owns its binary, so Windows can replace new without affecting
// other tests. Thresholds tolerate STL padding; counters prove each refusal.
void* operator new(size_t size) {
    recordAllocation(size);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void* operator new(size_t size, std::align_val_t align) {
    recordAllocation(size);
    if (void* p = _aligned_malloc(size ? size : 1, static_cast<size_t>(align))) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size, std::align_val_t align) { return ::operator new(size, align); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t align) noexcept { ::operator delete(p, align); }
void operator delete(void* p, size_t, std::align_val_t align) noexcept { ::operator delete(p, align); }
void operator delete[](void* p, size_t, std::align_val_t align) noexcept { ::operator delete(p, align); }
#endif
#endif
namespace {
int runLocalTextRead() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
        failures += !ok;
    };
    const auto dir = tc::fs::temp_directory_path() /
        ("tc-localTextRead-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    tc::fs::create_directories(dir);
    const auto path = dir / "text.txt";
    const std::string line(2u * 1024u * 1024u, 'x');
    {
        std::ofstream out(path, std::ios::binary);
        for (int i = 0; i < 9; ++i) out << line << "\r\n";
        out << "last";
    }
    auto bytes = tc::loadTextFile(path);
    check("local text above 16 MiB remains complete", bytes.size() == 9 * (line.size() + 2) + 4 &&
          bytes.substr(bytes.size() - 4) == "last");
    tc::FileReader reader;
    check("open local text", reader.open(path));
    std::string output;
    for (int i = 0; i < 9; ++i)
        check("overlong line is complete with CR stripped", reader.readLine(output) && output == line);
    check("unterminated final line", reader.readLine() == "last");
    check("EOF clears output", !reader.readLine(output) && output.empty());
    reader.close();
    output = line;
    check("closed reader releases output", !reader.readLine(output) && output.empty() && output.capacity() == std::string().capacity());
    // Growth happens after open, deterministically; it requires no sleep/race.
    reader.open(path);
    { std::ofstream out(path, std::ios::binary | std::ios::app); out << " appended\n"; }
    for (int i = 0; i < 9; ++i) reader.readLine();
    check("growth after open is read completely", reader.readLine() == "last appended");
    reader.close();
    { std::ofstream out(dir / "another.txt"); out << "data"; }
    check("directory retains required names", tc::listDirectory(dir).size() == 2);
    int inspectErrors = 0;
    std::string inspectMessage;
    {
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Error) { ++inspectErrors; inspectMessage = event.message; }
        });
        tc::listDirectory(dir / "absent");
        check("missing directory is silent", inspectErrors == 0);
        tc::listDirectory(path);
        check("regular file listing is silent", inspectErrors == 0);
        tc::listDirectory(path / "child");
        check("ENOTDIR listing is silent", inspectErrors == 0);
#ifdef __linux__
        if (geteuid() == 0) {
            std::puts("iterator Error includes path and reason: SKIP (root bypasses permissions)");
        } else {
            const auto denied = dir / "denied";
            tc::fs::create_directory(denied);
            tc::fs::permissions(denied, tc::fs::perms::none);
            inspectErrors = 0;
            const auto deniedResult = tc::listDirectory(denied);
            tc::fs::permissions(denied, tc::fs::perms::owner_all);
            check("iterator Error includes path and reason", deniedResult.empty() && inspectErrors == 1 &&
                inspectMessage.find(tc::pathToUtf8(denied)) != std::string::npos &&
                inspectMessage.find(std::make_error_code(std::errc::permission_denied).message()) != std::string::npos);
        }
#endif
    }
#if defined(TC_LOCAL_TEXT_WRAP_NEW) || defined(_WIN32)
    int allocationErrors = 0;
    {
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Error) ++allocationErrors;
        });
        bool allocationThrew = false;
        std::string failedRead;
        injectedRefusals = 0;
        failAllocationAt = 64 * 1024;
        try { failedRead = tc::loadTextFile(path); } catch (...) { allocationThrew = true; }
        failAllocationAt = 0;
        check("text allocation failure returns empty Error", injectedRefusals > 0 && !allocationThrew && failedRead.empty() && allocationErrors == 1);
        reader.open(path);
        std::string().swap(output);
        allocationErrors = 0;
        injectedRefusals = 0;
        failAllocationAt = 1024 * 1024;
        bool lineResult = reader.readLine(output);
        failAllocationAt = 0;
        check("line allocation failure returns empty Error", injectedRefusals > 0 && !lineResult && output.empty() && output.capacity() == std::string().capacity() && allocationErrors == 1);
        reader.close();
        allocationThrew = false;
        injectedRefusals = 0;
        // 64 bytes is comfortably above MSVC Debug's two-pointer iterator
        // proxy. Refuse payload/report growth, allowing noexcept empty strings.
        constexpr size_t persistentFaultBytes = 64;
        failAllocationAt = persistentFaultBytes; failEveryAllocation = true;
        try { failedRead = tc::loadTextFile(path); } catch (...) { allocationThrew = true; }
        failAllocationAt = 0; failEveryAllocation = false;
        check("persistent text/report allocation failure contained", injectedRefusals > 0 && !allocationThrew && failedRead.empty());
        reader.open(path);
        std::string().swap(output);
        injectedRefusals = 0;
        failAllocationAt = persistentFaultBytes; failEveryAllocation = true;
        lineResult = reader.readLine(output);
        failAllocationAt = 0; failEveryAllocation = false;
        check("persistent line/report allocation failure contained", injectedRefusals > 0 && !lineResult && output.empty());
        reader.close();
    }
#endif
    // A Notice callback appends after reading has begun, without scheduling
    // or timing assumptions. Sparse input keeps fixture creation cheap.
    const auto largePath = dir / "large.txt";
    { std::ofstream out(largePath, std::ios::binary); }
    constexpr size_t noticeBytes = 256u * 1024u * 1024u;
    // Leave unread bytes when the Notice fires: libc++ filebuf does not
    // pick up appends after it has already reached EOF.
    constexpr size_t fixtureBytes = noticeBytes + 8u * 1024u;
    tc::fs::resize_file(largePath, fixtureBytes);
    int notices = 0, warnings = 0;
    {
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.message.rfind("[File] ", 0) == 0 && event.level == tc::LogLevel::Notice) {
                ++notices;
                std::ofstream out(largePath, std::ios::binary | std::ios::app);
                out << "appended during read";
            }
            if (event.level == tc::LogLevel::Warning) ++warnings;
        });
#if defined(TC_LOCAL_TEXT_WRAP_NEW) || defined(_WIN32)
        largestAllocation = 0;
#endif
        auto large = tc::loadTextFile(largePath);
        check("large local read notices once and includes growth", notices == 1 && warnings == 0 &&
              large.size() == fixtureBytes + 20 && large.substr(fixtureBytes) == "appended during read");
#if defined(TC_LOCAL_TEXT_WRAP_NEW) || defined(_WIN32)
        check("single allocation stays proportional to bytes read", largestAllocation <= 2 * (large.size() + 65536));
#endif
    }
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // The kernel exposes a regular file that reports an I/O error at offset
    // zero. Exercise the read failure after opening, without changing modes.
    int readErrors = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.message.rfind("[File] ", 0) == 0 && event.level == tc::LogLevel::Error) ++readErrors;
    });
    bool threw = false;
    std::string unreadable;
    try { unreadable = tc::loadTextFile("/proc/self/mem"); } catch (...) { threw = true; }
    check("I/O failure returns empty and logs Error", !threw && unreadable.empty() && readErrors == 1);
#endif
    tc::fs::remove_all(dir);
    return failures ? 1 : 0;
}
}

TC_CORE_TEST_MAIN() { return runLocalTextRead(); }
