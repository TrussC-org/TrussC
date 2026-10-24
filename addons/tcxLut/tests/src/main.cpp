#include "../../../../core/tests/common/tcTestFailure.h"
#include <TrussC.h>
#include <tcLut.h>
#include <cstdio>
#include <limits>
#include <new>
#include <cstdlib>
namespace { bool failAll = false; size_t failedRequests = 0; }
void* operator new(size_t size) {
    if (failAll) { ++failedRequests; throw std::bad_alloc(); }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

int main() {
    int failures = 0;
    const auto check = [&](const char* label, bool ok) {
        std::printf("%-64s %s\n", label, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    };
    const auto path = std::filesystem::current_path() / std::filesystem::path(u8".lut-import-test-日本.cube");
    {
        std::ofstream file(path);
        file << "TITLE \"fixture\"\nLUT_3D_SIZE 2\n";
        for (int index = 0; index < 8; ++index) file << "0 0 0\n";
    }
    tcx::lut::Lut3D lut;
    tc::getLogger();
    bool hookRejected = false;
    failAll = true;
    try {
        void* block = ::operator new(sizeof(tcx::lut::Lut3D));
        ::operator delete(block);
    } catch (const std::bad_alloc&) { hookRejected = true; }
    failAll = false;
    check("global allocation-failure hook is active", hookRejected);
    failedRequests = 0;
    if (!tcTest::skipPersistentAllocationFailure("LUT file-entry path construction")) {
        bool returned = false, loaded = true;
        failAll = true;
        try { loaded = lut.load(path); returned = true; } catch (...) {}
        failAll = false;
        check("LUT file entry recovers from the first persistent allocation failure",
              returned && !loaded && failedRequests > 0 && !lut.isAllocated() &&
              lut.getSize() == 0 && lut.getTitle().empty());
    }
    std::string lastError;
    auto logs = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Error) lastError = event.message;
    });
    check("load reports failure when no GPU can create the texture", !lut.load(path) && !lut.isAllocated());
    check("LUT resource error names the non-ASCII file", lastError.find(tc::pathToUtf8(path)) != std::string::npos);
    { std::ofstream file(path); file << "LUT_3D_SIZE invalid\n"; }
    check("LUT parser error names the non-ASCII file", !lut.load(path) && lastError.find(tc::pathToUtf8(path)) != std::string::npos);
    lut.allocate(std::numeric_limits<int>::max());
    check("invalid direct allocation leaves an empty LUT", !lut.isAllocated() && lut.getSize() == 0);
    std::filesystem::remove(path);
    return failures ? 1 : 0;
}
