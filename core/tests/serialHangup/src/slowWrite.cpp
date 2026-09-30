// =============================================================================
// A write() that takes its time, for serialHangup (Linux only).
//
// Serial's port is O_NONBLOCK on macOS / Linux, so its writes never block.
// On Windows a WriteFile() to a slow device does (about 1 s for 64 KB to a
// SAMD21 over usbser.sys), and an Android bulk write can too. To play such a
// write here, this file defines write() itself, which takes precedence over
// libc's for every direct write() call in the test binary (Serial included),
// and forwards each call to libc. While setSlowWrite(fd, ms) is on, a write
// to that fd first waits ms.
//
// It also counts the writes that find their fd closed (EBADF): while a write
// is in progress, close() must wait for it.
//
// A separate file so that it does not see the write() declaration of
// <unistd.h>.
// =============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include <dlfcn.h>
#include <sys/types.h>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <ctime>

static std::atomic<int> g_slowFd{-1};
static std::atomic<int> g_slowMs{0};
static std::atomic<int> g_inSlowWrite{0};
static std::atomic<int> g_closedFdWrites{0};

// fd -1 turns it off
void setSlowWrite(int fd, int ms) {
    g_slowMs = ms;
    g_slowFd = fd;
}

// Writes to the slow fd that have started and not returned yet
int slowWritesInProgress() {
    return g_inSlowWrite;
}

// Writes to the slow fd that found it closed
int slowWritesToClosedFd() {
    return g_closedFdWrites;
}

extern "C" ssize_t write(int fd, const void* buf, size_t n) {
    using WriteFn = ssize_t (*)(int, const void*, size_t);
    static WriteFn real = reinterpret_cast<WriteFn>(dlsym(RTLD_NEXT, "write"));
    if (!real) {
        errno = ENOSYS;
        return -1;
    }
    bool slow = fd >= 0 && fd == g_slowFd;
    if (!slow) return real(fd, buf, n);

    ++g_inSlowWrite;
    int ms = g_slowMs;
    struct timespec wait = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&wait, &wait) == -1 && errno == EINTR) {}
    ssize_t result = real(fd, buf, n);
    int err = errno;
    if (result == -1 && err == EBADF) ++g_closedFdWrites;
    --g_inSlowWrite;
    errno = err;
    return result;
}

#endif
