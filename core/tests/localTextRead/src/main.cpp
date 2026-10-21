#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <chrono>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif
#ifdef TC_LOCAL_TEXT_WRAP_NEW
namespace {
thread_local size_t failAllocationAt = 0;
thread_local bool failEveryAllocation = false;
thread_local size_t largestAllocation = 0;
}
extern "C" void* __real__Znwm(size_t);
extern "C" void* __wrap__Znwm(size_t size) {
    largestAllocation = std::max(largestAllocation, size);
    if (failAllocationAt && size >= failAllocationAt) {
        if (!failEveryAllocation) failAllocationAt = 0;
        throw std::bad_alloc();
    }
    return __real__Znwm(size);
}
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
#ifdef TC_LOCAL_TEXT_WRAP_NEW
    int allocationErrors = 0;
    {
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Error) ++allocationErrors;
        });
        bool allocationThrew = false;
        std::string failedRead;
        failAllocationAt = 64 * 1024;
        try { failedRead = tc::loadTextFile(path); } catch (...) { allocationThrew = true; }
        failAllocationAt = 0;
        check("text allocation failure returns empty Error", !allocationThrew && failedRead.empty() && allocationErrors == 1);
        reader.open(path);
        std::string().swap(output);
        allocationErrors = 0;
        failAllocationAt = 1024 * 1024;
        bool lineResult = reader.readLine(output);
        failAllocationAt = 0;
        check("line allocation failure returns empty Error", !lineResult && output.empty() && output.capacity() == std::string().capacity() && allocationErrors == 1);
        reader.close();
        allocationThrew = false;
        failAllocationAt = 1; failEveryAllocation = true;
        try { failedRead = tc::loadTextFile(path); } catch (...) { allocationThrew = true; }
        failAllocationAt = 0; failEveryAllocation = false;
        check("persistent text/report allocation failure contained", !allocationThrew && failedRead.empty());
        reader.open(path);
        std::string().swap(output);
        failAllocationAt = 1; failEveryAllocation = true;
        lineResult = reader.readLine(output);
        failAllocationAt = 0; failEveryAllocation = false;
        check("persistent line/report allocation failure contained", !lineResult && output.empty());
        reader.close();
    }
#endif
    // A Notice callback appends after reading has begun, without scheduling
    // or timing assumptions. Sparse input keeps fixture creation cheap.
    const auto largePath = dir / "large.txt";
    { std::ofstream out(largePath, std::ios::binary); }
    constexpr size_t noticeBytes = 256u * 1024u * 1024u;
    tc::fs::resize_file(largePath, noticeBytes);
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
#ifdef TC_LOCAL_TEXT_WRAP_NEW
        largestAllocation = 0;
#endif
        auto large = tc::loadTextFile(largePath);
        check("large local read notices once and includes growth", notices == 1 && warnings == 0 &&
              large.size() == noticeBytes + 20 && large.substr(noticeBytes) == "appended during read");
#ifdef TC_LOCAL_TEXT_WRAP_NEW
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
