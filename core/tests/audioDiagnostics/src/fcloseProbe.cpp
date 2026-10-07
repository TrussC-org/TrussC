// =============================================================================
// An fclose() that counts, for audioDiagnostics (Linux only).
//
// SoundBuffer::loadOgg() opens the FILE* itself and hands it to stb_vorbis,
// which closes it. To check that the file is closed once, this file defines
// fclose() itself, which takes precedence over libc's for every fclose() call
// in the test binary (the statically linked TrussC and its stb_vorbis
// included), and forwards each call to libc.
//
// While armed, it counts the fclose() calls made on the thread that armed it
// and remembers the FILE* pointers they closed. A call on a pointer already
// closed in the armed window is counted as a repeat close and is not
// forwarded to libc (that FILE is gone), so the test itself stays safe.
// Calls on other threads, and all calls while disarmed, are only forwarded.
// =============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include <dlfcn.h>
#include <cerrno>
#include <cstdio>

namespace {

constexpr int kMaxRemembered = 64;

thread_local bool t_armed = false;   // this thread armed the probe

// Only touched by the armed thread
int g_closes = 0;
int g_repeats = 0;
FILE* g_closed[kMaxRemembered];
int g_closedCount = 0;

} // namespace

// Starts a new window on the calling thread (counts reset)
void fcloseProbeArm() {
    g_closes = 0;
    g_repeats = 0;
    g_closedCount = 0;
    t_armed = true;
}

void fcloseProbeDisarm() {
    t_armed = false;
}

// fclose() calls in the last window, repeats included
int fcloseProbeCloses() {
    return g_closes;
}

// Calls in the last window on a FILE* that window had already closed
int fcloseProbeRepeats() {
    return g_repeats;
}

extern "C" int fclose(FILE* f) {
    using FcloseFn = int (*)(FILE*);
    static FcloseFn real = reinterpret_cast<FcloseFn>(dlsym(RTLD_NEXT, "fclose"));
    if (!real) {
        errno = ENOSYS;
        return EOF;
    }
    if (!t_armed) return real(f);

    ++g_closes;
    for (int i = 0; i < g_closedCount; ++i) {
        if (g_closed[i] == f) {
            ++g_repeats;
            errno = EBADF;
            return EOF;
        }
    }
    if (g_closedCount < kMaxRemembered) g_closed[g_closedCount++] = f;
    return real(f);
}

#endif
