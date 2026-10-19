// =============================================================================
// Reads back the output baud rate of a tty, as a number, for serialBaudRate.
//
// A separate file because the Linux side needs <asm/termbits.h> (termios2,
// which reports any rate, BOTHER ones included), and that header clashes with
// the <termios.h> that TrussC.h pulls in.
// =============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include <asm/termbits.h>
#include <sys/ioctl.h>

// On a pty master, termios ioctls act on the slave: this is what Serial set.
long readOutputBaud(int fd) {
    struct termios2 tio;
    if (ioctl(fd, TCGETS2, &tio) == -1) return -1;
    return (long)tio.c_ospeed;
}

#elif !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include <termios.h>

// macOS / BSD: master and slave share one termios, and speed_t is the rate.
long readOutputBaud(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) == -1) return -1;
    return (long)cfgetospeed(&t);
}

#endif
