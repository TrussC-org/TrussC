// =============================================================================
// A global operator new that remembers the largest request, for
// audioDiagnostics.
//
// The decoders must not size a buffer from a stream's stated length. To see
// what a load asked for, this file replaces the global operator new / delete
// (plain and array forms; the nothrow forms forward to them) for the whole
// test binary, the statically linked TrussC included. Allocation itself is
// plain malloc / free.
//
// While armed, the largest single request made on the thread that armed the
// probe is recorded. Requests on other threads (the audio callback), and all
// requests while disarmed, are only served.
// =============================================================================

#include <cstdlib>
#include <new>

namespace {

thread_local bool t_armed = false;   // this thread armed the probe
size_t g_largest = 0;                // only touched by the armed thread

void* allocate(size_t n) {
    if (t_armed && n > g_largest) g_largest = n;
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

void* operator new(size_t n) { return allocate(n); }
void* operator new[](size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
