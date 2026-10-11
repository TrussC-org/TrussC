// =============================================================================
// A global operator new that remembers the largest request, for
// audioDiagnostics.
//
// The decoders must not size a buffer from a stream's stated length. To see
// what a load asked for, this file replaces the global operator new / delete
// (plain, array and nothrow forms share the allocator) for the whole
// test binary, the statically linked TrussC included. Allocation itself is
// plain malloc / free.
//
// While armed, the largest single request made on the thread that armed the
// probe is recorded. Requests on other threads (the audio callback), and all
// requests while disarmed, are only served.
//
// allocProbeFailAbove() makes requests larger than a limit throw
// std::bad_alloc, on the calling thread only, as a failed allocation near a
// memory limit would; other threads are not affected.
// =============================================================================

#include <cstdlib>
#include <new>

namespace {

thread_local bool t_armed = false;   // this thread armed the probe
size_t g_largest = 0;                // only touched by the armed thread
thread_local size_t t_failAbove = 0; // 0: no limit on this thread

void* allocate(size_t n) {
    if (t_armed && n > g_largest) g_largest = n;
    if (t_failAbove != 0 && n > t_failAbove) throw std::bad_alloc();
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

} // namespace

// Starts a new window on the calling thread (largest request reset)
void allocProbeArm() {
    g_largest = 0;
    t_armed = true;
}

void allocProbeDisarm() { t_armed = false; }

// Largest single operator new request (bytes) in the last window
size_t allocProbeLargest() { return g_largest; }

// Requests on the calling thread larger than `bytes` throw std::bad_alloc
// from now on; 0 turns this off
void allocProbeFailAbove(size_t bytes) { t_failAbove = bytes; }

void* operator new(size_t n) { return allocate(n); }
void* operator new[](size_t n) { return allocate(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
