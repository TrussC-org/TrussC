// =============================================================================
// tcSerial.cpp - Serial baud rates without a termios B-constant (POSIX)
//
// Serial::setup() applies the port settings with tcsetattr() and a
// placeholder speed, then calls setSerialCustomBaudRate() for rates that
// <termios.h> has no B-constant for (250000, 921600 on macOS, 74880, ...).
//
// This file must not include <termios.h> (so not tcSerial.h either): on Linux
// it uses <asm/termbits.h>, whose struct termios clashes with glibc's.
// =============================================================================

#if !defined(_WIN32) && !defined(__ANDROID__)

#include <cerrno>

#if defined(__linux__) && __has_include(<asm/termbits.h>)
    // Linux: termios2 + BOTHER takes the rate as a number
    #include <asm/termbits.h>
    #include <sys/ioctl.h>
#elif defined(__APPLE__) && __has_include(<IOKit/serial/ioss.h>)
    // macOS: IOSSIOSPEED sets any rate the driver supports
    #include <termios.h>
    #include <sys/ioctl.h>
    #include <IOKit/serial/ioss.h>
#endif

namespace trussc {
namespace internal {

// Declared in tcSerial.h
bool setSerialCustomBaudRate(int fd, int baudRate, int& appliedBaudRate) {
    if (baudRate <= 0) {
        errno = EINVAL;
        return false;
    }

#if defined(__linux__) && defined(TCSETS2) && defined(BOTHER) && defined(IBSHIFT)
    struct termios2 tio;
    if (ioctl(fd, TCGETS2, &tio) == -1) return false;
    // Output and input speed both from c_ospeed / c_ispeed
    tio.c_cflag &= ~(CBAUD | (CBAUD << IBSHIFT));
    tio.c_cflag |= BOTHER | (BOTHER << IBSHIFT);
    tio.c_ospeed = static_cast<speed_t>(baudRate);
    tio.c_ispeed = static_cast<speed_t>(baudRate);
    if (ioctl(fd, TCSETS2, &tio) == -1) return false;
    // The driver writes back the rate it could actually generate
    if (ioctl(fd, TCGETS2, &tio) == -1) return false;
    appliedBaudRate = static_cast<int>(tio.c_ospeed);
    return true;
#elif defined(__APPLE__) && defined(IOSSIOSPEED)
    // Must come after tcsetattr(), which would reset it. There is no
    // matching getter, so report the requested rate.
    speed_t speed = static_cast<speed_t>(baudRate);
    if (ioctl(fd, IOSSIOSPEED, &speed) == -1) return false;
    appliedBaudRate = baudRate;
    return true;
#else
    (void)fd;
    (void)appliedBaudRate;
    errno = ENOTSUP;
    return false;
#endif
}

} // namespace internal
} // namespace trussc

#endif // !_WIN32 && !__ANDROID__
