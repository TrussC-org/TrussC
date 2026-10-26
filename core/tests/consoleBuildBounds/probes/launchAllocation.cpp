// Standalone allocator override: keep it out of the combined test executable.
// c++ -std=c++20 -Icore/include <this file> -o /tmp/launchAllocation
#include "tc/app/internal/tcPosixBuildOutput.h"
#include <new>
#include <dirent.h>
static bool denyNew = false;
void* operator new(std::size_t bytes) {
    if (!denyNew) if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
// No output is produced: this probe exercises launch admission only.
namespace trussc::internal {
BuildOutputLoss& buildOutputLoss() { static BuildOutputLoss loss; return loss; }
void BuildOutputLoss::record(uint64_t) {}
void BuildOutputLoss::recordStorageFailure() {}
void BuildOutputLoss::recordForwardingFailure() {}
}
static int descriptors() {
    DIR* entries = opendir("/proc/self/fd");
    if (!entries) return -1;
    int count = 0;
    while (auto* entry = readdir(entries)) if (entry->d_name[0] != '.') ++count;
    closedir(entries);
    return count;
}
int main() {
    const std::vector<std::string> args{"/bin/true"};
    std::string output;
    const int before = descriptors();
    bool cleanFailure = true;
    for (int i = 0; i < 32; ++i) {
        denyNew = true;
        try { if (trussc::internal::runPosixBuildWithOutput(args, output) != -1) cleanFailure = false; }
        catch (...) { cleanFailure = false; }
        denyNew = false;
    }
    const int after = descriptors();
    std::printf("launch allocation failure: descriptors %d -> %d; clean failure=%d\n", before, after, cleanFailure);
    return before >= 0 && before == after && cleanFailure ? 0 : 1;
}
