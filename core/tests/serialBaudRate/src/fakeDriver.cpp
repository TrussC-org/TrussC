// =============================================================================
// Plays a Linux serial driver that swaps in another baud rate, for
// serialBaudRate (Linux only).
//
// A real driver that cannot generate a rate (ftdi_sio above the chip maximum,
// cp210x clamping to its range) does not fail tcsetattr() or TCSETS2: it
// writes another rate back into the termios, which TCGETS2 then reports. A
// pty never does that. So this file defines ioctl() itself, which takes
// precedence over libc's for every direct ioctl() call in the test binary
// (Serial included), forwards each call to libc, and while a fake rate is
// set, rewrites the output rate TCGETS2 reports.
//
// A separate file because it must not see the ioctl() declaration of
// <sys/ioctl.h> (glibc marks it noexcept for C++, musl does not), and
// <asm/termbits.h> clashes with the <termios.h> that TrussC.h pulls in.
// =============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <dlfcn.h>
#include <atomic>
#include <cerrno>
#include <cstdarg>

static std::atomic<unsigned> g_fakeRate{0};

// 0 turns the fake off
void setFakeDriverRate(unsigned rate) {
    g_fakeRate = rate;
}

extern "C" int ioctl(int fd, unsigned long request, ...) {
    // Every ioctl takes at most one argument, which libc reads as a pointer too
    va_list ap;
    va_start(ap, request);
    void* arg = va_arg(ap, void*);
    va_end(ap);

    using IoctlFn = int (*)(int, unsigned long, ...);
    IoctlFn real = reinterpret_cast<IoctlFn>(dlsym(RTLD_NEXT, "ioctl"));
    if (!real) {
        errno = ENOSYS;
        return -1;
    }
    int result = real(fd, request, arg);

    unsigned fake = g_fakeRate;
    if (result == 0 && request == TCGETS2 && fake != 0) {
        static_cast<struct termios2*>(arg)->c_ospeed = fake;
    }
    return result;
}

#endif
