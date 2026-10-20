// Observe actual vendor malloc requests, including ones outside operator new.
// --wrap is enabled only on native Linux for this and the combined harness.
#if defined(__linux__) && !defined(__ANDROID__)
#include <cstddef>
namespace {
thread_local bool armed = false;
thread_local size_t largest = 0;
}
void audioMallocProbeArm() { largest = 0; armed = true; }
size_t audioMallocProbeDisarm() { armed = false; return largest; }
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t bytes) {
    if (armed) {
        if (bytes > largest) largest = bytes;
        // A regression records the giant request without consuming real memory.
        if (bytes > 1024 * 1024) return nullptr;
    }
    return __real_malloc(bytes);
}
#endif
