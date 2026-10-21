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
    const auto path = std::filesystem::current_path() / ".lut-import-test.cube";
    {
        std::ofstream file(path);
        file << "TITLE \"fixture\"\nLUT_3D_SIZE 2\n";
        for (int index = 0; index < 8; ++index) file << "0 0 0\n";
    }
    tcx::lut::Lut3D lut;
    tc::getLogger();
    bool returned = false, loaded = true;
    failAll = true;
    try { loaded = lut.load(path); returned = true; } catch (...) {}
    failAll = false;
    check("LUT file entry recovers from the first persistent allocation failure",
          returned && !loaded && failedRequests > 0 && !lut.isAllocated() &&
          lut.getSize() == 0 && lut.getTitle().empty());
    check("load reports failure when no GPU can create the texture", !lut.load(path) && !lut.isAllocated());
    lut.allocate(std::numeric_limits<int>::max());
    check("invalid direct allocation leaves an empty LUT", !lut.isAllocated() && lut.getSize() == 0);
    std::filesystem::remove(path);
    return failures ? 1 : 0;
}
