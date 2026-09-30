// =============================================================================
// Plays Linux serial drivers that do not apply the baud rate asked for, for
// serialBaudRate (Linux only).
//
// A real driver that cannot generate a rate (ftdi_sio above the chip maximum,
// cp210x clamping to its range) does not fail tcsetattr() or TCSETS2: it
// writes another rate back into the termios, which TCGETS2 then reports. It
// still applies the rates it can generate. A driver without set_termios
// (u_serial /dev/ttyGS*, usb_serial_generic, ...) applies no rate at all: the
// kernel restores the old one after every change. A pty does neither. So
// this file defines ioctl() itself, which takes precedence over libc's for
// every direct ioctl() call in the test binary (Serial included), forwards
// each call to libc, and rewrites the output rate TCGETS2 reports:
//  - setFakeDriverSwap(from, to): `to` while the tty is set to `from`, the
//    real rate otherwise (a driver that swaps one rate it cannot generate);
//  - setFakeDriverRate(rate): always `rate` (a driver that keeps its rate).
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
static std::atomic<unsigned> g_swapFrom{0};
static std::atomic<unsigned> g_swapTo{0};

// 0 turns the fake off
void setFakeDriverRate(unsigned rate) {
    g_fakeRate = rate;
}

// from = 0 turns the fake off
void setFakeDriverSwap(unsigned from, unsigned to) {
    g_swapFrom = from;
    g_swapTo = to;
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

    if (result == 0 && request == TCGETS2) {
        auto* tio = static_cast<struct termios2*>(arg);
        unsigned fake = g_fakeRate;
        unsigned from = g_swapFrom;
        if (fake != 0) {
            tio->c_ospeed = fake;
        } else if (from != 0 && tio->c_ospeed == from) {
            tio->c_ospeed = g_swapTo;
        }
    }
    return result;
}

#endif
