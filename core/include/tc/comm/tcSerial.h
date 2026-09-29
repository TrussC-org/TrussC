#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// TrussC Serial Communication
// Cross-platform serial communication class
// - Windows: Win32 API (CreateFile, SetCommState, etc.)
// - macOS/Linux: POSIX API (termios). Baud rates without a termios B-constant
//   are set through IOSSIOSPEED (macOS) or termios2 / BOTHER (Linux), see
//   tcSerial.cpp.
// - Android: USB Host API (CDC-ACM devices only), JNI + usbfs.
//   See platform/android/tcSerial_android.cpp for details and caveats.
//
// Device loss (e.g. a USB-serial adapter unplugged): available(), readBytes(),
// readByte() and writeBytes() detect it, close the port and log one warning,
// so isConnected() turns false and the app can call setup() again.
//
// Not thread-safe: since those calls may close the port, an app that uses one
// Serial from several threads must guard every call with one mutex.
// =============================================================================

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstring>

// Platform-specific headers
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__ANDROID__)
    // Android: no platform headers needed here; the backend lives in
    // platform/android/tcSerial_android.cpp (USB Host CDC-ACM via JNI + usbfs).
#else
    // POSIX (macOS, Linux)
    #include <fcntl.h>
    #include <unistd.h>
    #include <termios.h>
    #include <sys/ioctl.h>
    #include <poll.h>
    #include <dirent.h>
    #include <cerrno>
#endif

#include "../utils/tcLog.h"

namespace trussc {

// ---------------------------------------------------------------------------
// Serial Device Info
// ---------------------------------------------------------------------------

struct SerialDeviceInfo {
    int deviceId;           // Device index
    std::string devicePath; // Device path (e.g., COM3, /dev/tty.usbserial-A10172HG)
    std::string deviceName; // Device name

    int getDeviceID() const { return deviceId; }
    const std::string& getDevicePath() const { return devicePath; }
    const std::string& getDeviceName() const { return deviceName; }
};

#if defined(__ANDROID__)
// ---------------------------------------------------------------------------
// Android backend (USB Host, CDC-ACM class devices).
// Implemented in platform/android/tcSerial_android.cpp.
//
// Behavior difference vs desktop: opening a USB device requires a one-time
// user permission dialog. setup() returns false while the dialog is pending
// and the connection completes asynchronously; isInitialized() flips to true
// once the user grants access. Calling setup() again for the same device
// while pending is a cheap no-op (safe to call from a reconnect loop).
// ---------------------------------------------------------------------------
namespace androidserial {
    struct Impl;
    Impl* create();
    void destroy(Impl* impl);
    std::vector<SerialDeviceInfo> listDevices();
    bool setup(Impl* impl, const std::string& devicePath, int baudRate);
    void close(Impl* impl);
    bool isConnected(const Impl* impl);
    int available(const Impl* impl);
    int readBytes(Impl* impl, void* buffer, int length);
    int writeBytes(Impl* impl, const void* buffer, int length);
    void flushInput(Impl* impl);
}
#endif

#if !defined(_WIN32) && !defined(__ANDROID__)
namespace internal {
    // Apply a baud rate that has no termios B-constant to an open serial fd,
    // after tcsetattr() has applied the other settings: IOSSIOSPEED on macOS,
    // termios2 with BOTHER on Linux. Returns false with errno set when the
    // driver rejects the rate, or ENOTSUP when the platform has neither.
    // On success appliedBaudRate is the rate the driver reports back (Linux),
    // or the requested rate where there is no read-back (macOS).
    // Lives in tcSerial.cpp: <asm/termbits.h> clashes with <termios.h>.
    bool setSerialCustomBaudRate(int fd, int baudRate, int& appliedBaudRate);

    // Whether the rate a driver reports back counts as the requested one.
    // A Linux driver that cannot generate a rate does not fail the ioctl: it
    // writes back another rate instead, often the previous one or 9600. A
    // driver's nearest divisor stays close, so allow the 2% the kernel itself
    // uses when it matches a rate to a B-constant (tty_termios_encode_baud_rate).
    inline bool isBaudRateClose(int requested, int applied) {
        long long diff = static_cast<long long>(applied) - requested;
        long long allowed = requested / 50;
        return diff >= -allowed && diff <= allowed;
    }
}
#endif

// ---------------------------------------------------------------------------
// Serial Communication Class
// ---------------------------------------------------------------------------

class TC_PLATFORMS("macos,windows,linux,android") Serial {
public:
#if defined(_WIN32)
    Serial() : handle_(INVALID_HANDLE_VALUE), initialized_(false) {}
#elif defined(__ANDROID__)
    Serial() : aimpl_(androidserial::create()), initialized_(false) {}
#else
    Serial() : fd_(-1), initialized_(false) {}
#endif

    ~Serial() {
        close();
#if defined(__ANDROID__)
        androidserial::destroy(aimpl_);
        aimpl_ = nullptr;
#endif
    }

    // Non-copyable
    Serial(const Serial&) = delete;
    Serial& operator=(const Serial&) = delete;

    // Move-enabled
    Serial(Serial&& other) noexcept
#if defined(_WIN32)
        : handle_(other.handle_), initialized_(other.initialized_), devicePath_(std::move(other.devicePath_)) {
        other.handle_ = INVALID_HANDLE_VALUE;
#elif defined(__ANDROID__)
        : aimpl_(other.aimpl_), initialized_(other.initialized_), devicePath_(std::move(other.devicePath_)) {
        other.aimpl_ = nullptr;
#else
        : fd_(other.fd_), initialized_(other.initialized_), devicePath_(std::move(other.devicePath_)) {
        other.fd_ = -1;
#endif
        other.initialized_ = false;
    }

    Serial& operator=(Serial&& other) noexcept {
        if (this != &other) {
            close();
#if defined(_WIN32)
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
#elif defined(__ANDROID__)
            androidserial::destroy(aimpl_);
            aimpl_ = other.aimpl_;
            other.aimpl_ = nullptr;
#else
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
            initialized_ = other.initialized_;
            devicePath_ = std::move(other.devicePath_);
            other.initialized_ = false;
        }
        return *this;
    }

    // ---------------------------------------------------------------------------
    // Device Enumeration
    // ---------------------------------------------------------------------------

    // Print available serial devices to the log.
    static void printDevices() {
        auto devices = listDevices();
        logNotice() << "Serial devices:";
        for (const auto& dev : devices) {
            logNotice() << "  [" << dev.deviceId << "] " << dev.devicePath;
        }
    }

    // Get the list of available serial devices. Follows the TrussC convention
    // (AudioEngine::listDevices(), MidiIn::listDevices(), ...): returns a
    // vector. Use printDevices() to log them instead.
    static std::vector<SerialDeviceInfo> listDevices() {
        std::vector<SerialDeviceInfo> devices;

#if defined(_WIN32)
        // Windows: Try COM1 to COM256
        int id = 0;
        for (int i = 1; i <= 256; i++) {
            std::string portName = "COM" + toString(i);
            std::string fullPath = "\\\\.\\" + portName;

            HANDLE hPort = CreateFileA(fullPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                       0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hPort != INVALID_HANDLE_VALUE) {
                CloseHandle(hPort);
                SerialDeviceInfo info;
                info.deviceId = id++;
                info.devicePath = portName;
                info.deviceName = portName;
                devices.push_back(info);
            }
        }
#elif defined(__APPLE__)
        // macOS: Enumerate /dev/tty.* and /dev/cu.*
        DIR* dir = opendir("/dev");
        if (dir) {
            struct dirent* entry;
            int id = 0;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                // Filter for tty.usbserial, tty.usbmodem, cu.*, etc.
                if (name.find("tty.usb") == 0 || name.find("cu.usb") == 0 ||
                    name.find("tty.serial") == 0 || name.find("cu.serial") == 0 ||
                    name.find("tty.SLAB") == 0 || name.find("cu.SLAB") == 0 ||
                    name.find("tty.wch") == 0 || name.find("cu.wch") == 0) {
                    SerialDeviceInfo info;
                    info.deviceId = id++;
                    info.devicePath = "/dev/" + name;
                    info.deviceName = name;
                    devices.push_back(info);
                }
            }
            closedir(dir);
        }
#elif defined(__ANDROID__)
        // Android: enumerate CDC-ACM capable devices via USB Host API.
        // devicePath is the usbfs node (e.g. /dev/bus/usb/001/002).
        devices = androidserial::listDevices();
#elif defined(__linux__)
        // Linux: Enumerate /dev/ttyUSB*, /dev/ttyACM*
        DIR* dir = opendir("/dev");
        if (dir) {
            struct dirent* entry;
            int id = 0;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0 ||
                    name.find("ttyS") == 0) {
                    SerialDeviceInfo info;
                    info.deviceId = id++;
                    info.devicePath = "/dev/" + name;
                    info.deviceName = name;
                    devices.push_back(info);
                }
            }
            closedir(dir);
        }
#endif

        return devices;
    }

    // Deprecated alias for listDevices().
    [[deprecated("Use listDevices() instead. Will be removed in v1.0.0")]]
    std::vector<SerialDeviceInfo> getDeviceList() { return listDevices(); }

    // ---------------------------------------------------------------------------
    // Connection
    // ---------------------------------------------------------------------------

    // Connect by specifying device path
    bool setup(const std::string& portName, int baudRate) {
#if defined(__ANDROID__)
        // Do NOT close() first here: androidserial::setup() keeps a pending
        // permission request alive when called again for the same device
        // (reconnect loops must not re-trigger the permission dialog).
        if (!aimpl_) aimpl_ = androidserial::create();
        devicePath_ = portName;
        initialized_ = androidserial::setup(aimpl_, portName, baudRate);
        return initialized_;
#else
        close();
#endif

#if defined(_WIN32)
        // Windows: Open COM port
        std::string fullPath = portName;
        // Use \\.\COMxx format for COM10 and above
        if (portName.find("\\\\.\\") != 0) {
            fullPath = "\\\\.\\" + portName;
        }

        handle_ = CreateFileA(fullPath.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0,                     // Exclusive access
                              nullptr,               // No security attributes
                              OPEN_EXISTING,
                              0,                     // Non-overlapped mode
                              nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            logError() << "Serial: failed to open " << portName << " (error: " << GetLastError() << ")";
            return false;
        }

        // Timeout settings (near non-blocking behavior)
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
        SetCommTimeouts(handle_, &timeouts);

        // Get DCB settings
        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle_, &dcb)) {
            logError() << "Serial: failed to get comm state";
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // Set baud rate
        dcb.BaudRate = baudRate;
        dcb.ByteSize = 8;               // 8 data bits
        dcb.Parity = NOPARITY;          // No parity
        dcb.StopBits = ONESTOPBIT;      // 1 stop bit
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;       // Disable CTS flow control
        dcb.fOutxDsrFlow = FALSE;       // Disable DSR flow control
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = FALSE;              // Disable XON/XOFF output
        dcb.fInX = FALSE;               // Disable XON/XOFF input
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fAbortOnError = FALSE;

        if (!SetCommState(handle_, &dcb)) {
            logError() << "Serial: failed to set comm state";
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // Clear buffers
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

        devicePath_ = portName;
        initialized_ = true;
        logNotice() << "Serial: connected to " << portName << " at " << baudRate << " baud";
        return true;

#elif !defined(__ANDROID__)
        // POSIX (macOS, Linux)
        // Reject a nonsense rate before opening: opening asserts DTR, which
        // resets auto-reset boards such as most Arduinos.
        if (baudRate <= 0) {
            logError() << "Serial: invalid baud rate " << baudRate;
            return false;
        }

        // Open device (non-blocking)
        fd_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ == -1) {
            logError() << "Serial: failed to open " << portName;
            return false;
        }

        // Get exclusive lock
        if (ioctl(fd_, TIOCEXCL) == -1) {
            logError() << "Serial: failed to get exclusive access";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // Get terminal settings
        struct termios options;
        if (tcgetattr(fd_, &options) == -1) {
            logError() << "Serial: failed to get terminal attributes";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // Set raw mode (no input/output processing)
        cfmakeraw(&options);

        // Set baud rate. A rate without a B-constant opens with a placeholder
        // here and gets its real value from setSerialCustomBaudRate() below.
        std::optional<speed_t> speed = baudRateToSpeed(baudRate);
        cfsetispeed(&options, speed ? *speed : B9600);
        cfsetospeed(&options, speed ? *speed : B9600);

        // 8N1 (8 data bits, no parity, 1 stop bit)
        options.c_cflag &= ~PARENB;  // No parity
        options.c_cflag &= ~CSTOPB;  // 1 stop bit
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;       // 8 data bits

        // Local connection, enable receiver
        options.c_cflag |= (CLOCAL | CREAD);

        // Disable hardware flow control
        options.c_cflag &= ~CRTSCTS;

        // Disable software flow control
        options.c_iflag &= ~(IXON | IXOFF | IXANY);

        // Minimum receive character count and wait time
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;

        // Apply settings
        if (tcsetattr(fd_, TCSANOW, &options) == -1) {
            logError() << "Serial: failed to set terminal attributes";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        int appliedBaudRate = baudRate;
        if (!speed && !internal::setSerialCustomBaudRate(fd_, baudRate, appliedBaudRate)) {
            int err = errno;
            logError() << "Serial: cannot set " << baudRate << " baud on " << portName
                       << " (" << std::strerror(err) << ")";
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        // A driver that could not generate the rate may have applied another
        // one (see isBaudRateClose()): that is a failure, not a connection at
        // the wrong speed.
        if (!internal::isBaudRateClose(baudRate, appliedBaudRate)) {
            logError() << "Serial: cannot set " << baudRate << " baud on " << portName
                       << " (the driver applied " << appliedBaudRate << ")";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // Flush buffers
        tcflush(fd_, TCIOFLUSH);

        devicePath_ = portName;
        initialized_ = true;
        if (appliedBaudRate != baudRate) {
            logWarning() << "Serial: requested " << baudRate << " baud on " << portName
                         << ", the driver applied " << appliedBaudRate;
        }
        logNotice() << "Serial: connected to " << portName << " at " << appliedBaudRate << " baud";
        return true;
#endif
    }

    // Connect by specifying device index
    bool setup(int deviceIndex, int baudRate) {
        auto devices = listDevices();
        if (deviceIndex < 0 || deviceIndex >= (int)devices.size()) {
            logError() << "Serial: device index " << deviceIndex << " out of range (0-" << (int)devices.size() - 1 << ")";
            return false;
        }
        return setup(devices[deviceIndex].devicePath, baudRate);
    }

    // Disconnect
    void close() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            logVerbose() << "Serial: disconnected from " << devicePath_;
        }
#elif defined(__ANDROID__)
        if (aimpl_) androidserial::close(aimpl_);
#else
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
            logVerbose() << "Serial: disconnected from " << devicePath_;
        }
#endif
        initialized_ = false;
    }

    // ---------------------------------------------------------------------------
    // Status
    // ---------------------------------------------------------------------------

    // Whether the port is open and working. Turns false after close(), and
    // also when available() / readBytes() / readByte() / writeBytes() find
    // that the device went away (USB unplug, driver reset): the port is then
    // closed, and setup() connects again. This call does not probe the
    // device itself; detection happens inside those I/O calls.
    // Android: may flip to true AFTER setup() returned false, once the user
    // grants USB permission (see androidserial note above).
    bool isConnected() const {
#if defined(_WIN32)
        return initialized_ && handle_ != INVALID_HANDLE_VALUE;
#elif defined(__ANDROID__)
        return aimpl_ && androidserial::isConnected(aimpl_);
#else
        return initialized_ && fd_ != -1;
#endif
    }

    // Same as isConnected(). The older name, kept for existing code.
    bool isInitialized() const {
        return isConnected();
    }

    // Get current device path
    const std::string& getDevicePath() const {
        return devicePath_;
    }

    // Get number of bytes available for reading.
    // Returns 0 when not connected; a device loss found here closes the port
    // (isConnected() turns false).
    int available() const {
        if (!isConnected()) return 0;

#if defined(_WIN32)
        COMSTAT comStat;
        DWORD errors;
        if (ClearCommError(handle_, &errors, &comStat)) {
            return (int)comStat.cbInQue;
        }
        markDisconnected("ClearCommError", GetLastError());
        return 0;
#elif defined(__ANDROID__)
        return androidserial::available(aimpl_);
#else
        int bytesAvailable = 0;
        if (ioctl(fd_, FIONREAD, &bytesAvailable) == -1) {
            int err = errno;
            if (isDeviceLostError(err)) markDisconnected("ioctl(FIONREAD)", err);
            return 0;
        }
        if (bytesAvailable > 0) return bytesAvailable;
        // Nothing buffered: tell a quiet port from a hung-up one
        if (isHungUp()) markDisconnected("hangup", 0);
        return 0;
#endif
    }

    // ---------------------------------------------------------------------------
    // Reading
    // ---------------------------------------------------------------------------

    // Read specified number of bytes
    // Returns: actual bytes read (>=0), -1 on error. A device loss found here
    // closes the port (isConnected() turns false) and returns -1.
    int readBytes(void* buffer, int length) {
        if (!isConnected()) return -1;
        if (length <= 0) return 0;

#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, buffer, length, &bytesRead, nullptr)) {
            markDisconnected("ReadFile", GetLastError());
            return -1;
        }
        return (int)bytesRead;
#elif defined(__ANDROID__)
        return androidserial::readBytes(aimpl_, buffer, length);
#else
        ssize_t result = read(fd_, buffer, length);
        if (result > 0) return static_cast<int>(result);
        if (result == -1) {
            int err = errno;
            // EAGAIN/EWOULDBLOCK means "no data available", like 0 below
            if (err != EAGAIN && err != EWOULDBLOCK) {
                if (isDeviceLostError(err)) markDisconnected("read", err);
                return -1;
            }
        }
        // No data. With VMIN = VTIME = 0, read() returns 0 both for a quiet
        // port and for a hung-up tty, so only poll() can tell them apart.
        if (isHungUp()) {
            markDisconnected("hangup", 0);
            return -1;
        }
        return 0;
#endif
    }

    // Read into std::string (convenience method)
    int readBytes(std::string& buffer, int length) {
        buffer.resize(length);
        int result = readBytes(buffer.data(), length);
        if (result >= 0) {
            buffer.resize(result);
        }
        return result;
    }

    // Read single byte
    // Returns: byte read (0-255), -1 if no data, -2 on error. A device loss
    // found here closes the port (isConnected() turns false) and returns -2.
    int readByte() {
        if (!isConnected()) return -2;

        unsigned char byte;
#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, &byte, 1, &bytesRead, nullptr)) {
            markDisconnected("ReadFile", GetLastError());
            return -2;  // Error
        }
        if (bytesRead == 1) {
            return byte;
        }
        return -1;  // No data
#elif defined(__ANDROID__)
        int result = androidserial::readBytes(aimpl_, &byte, 1);
        if (result == 1) return byte;
        if (result == 0) return -1;  // No data
        return -2;  // Error
#else
        ssize_t result = read(fd_, &byte, 1);
        if (result == 1) return byte;
        if (result == -1) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                if (isDeviceLostError(err)) markDisconnected("read", err);
                return -2;  // Error
            }
        }
        // No data, or a hung-up tty (see readBytes())
        if (isHungUp()) {
            markDisconnected("hangup", 0);
            return -2;
        }
        return -1;  // No data
#endif
    }

    // ---------------------------------------------------------------------------
    // Writing
    // ---------------------------------------------------------------------------

    // Write specified number of bytes
    // Returns: actual bytes written, -1 on error. A device loss found here
    // closes the port (isConnected() turns false) and returns -1.
    int writeBytes(const void* buffer, int length) {
        if (!isConnected()) return -1;
        if (length <= 0) return 0;

#if defined(_WIN32)
        DWORD bytesWritten = 0;
        if (!WriteFile(handle_, buffer, length, &bytesWritten, nullptr)) {
            markDisconnected("WriteFile", GetLastError());
            return -1;
        }
        return (int)bytesWritten;
#elif defined(__ANDROID__)
        return androidserial::writeBytes(aimpl_, buffer, length);
#else
        ssize_t result = write(fd_, buffer, length);
        if (result == -1) {
            int err = errno;
            if (isDeviceLostError(err)) markDisconnected("write", err);
            return -1;
        }
        return static_cast<int>(result);
#endif
    }

    // Write std::string
    int writeBytes(const std::string& buffer) {
        return writeBytes(buffer.data(), static_cast<int>(buffer.size()));
    }

    // Write single byte
    bool writeByte(unsigned char byte) {
        return writeBytes(&byte, 1) == 1;
    }

    // ---------------------------------------------------------------------------
    // Buffer Control
    // ---------------------------------------------------------------------------

    // Flush input buffer
    void flushInput() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_RXCLEAR);
#elif defined(__ANDROID__)
        androidserial::flushInput(aimpl_);
#else
        tcflush(fd_, TCIFLUSH);
#endif
    }

    // Flush output buffer
    void flushOutput() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_TXCLEAR);
#elif defined(__ANDROID__)
        // No-op: Android writes are synchronous bulk transfers
#else
        tcflush(fd_, TCOFLUSH);
#endif
    }

    // Flush both input and output buffers
    void flush() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
#elif defined(__ANDROID__)
        androidserial::flushInput(aimpl_);
#else
        tcflush(fd_, TCIOFLUSH);
#endif
    }

    // Wait until output completes
    void drain() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        FlushFileBuffers(handle_);
#elif defined(__ANDROID__)
        // No-op: Android writes are synchronous bulk transfers
#else
        tcdrain(fd_);
#endif
    }

private:
    // mutable: the const available() closes the port when it finds the
    // device gone (see markDisconnected()).
#if defined(_WIN32)
    mutable HANDLE handle_;        // Windows handle
#elif defined(__ANDROID__)
    androidserial::Impl* aimpl_;  // Android backend state
#else
    mutable int fd_;               // File descriptor (POSIX)
#endif
    mutable bool initialized_;     // Connection state
    std::string devicePath_; // Current device path

#if defined(_WIN32)
    // The device went away: close the handle (a stale one would block
    // reopening the COM port) and log once; later calls see !isConnected().
    // Any failure of ClearCommError / ReadFile / WriteFile counts as a loss:
    // with fAbortOnError off, line errors do not fail them, and the error
    // code a removed device returns differs between drivers.
    void markDisconnected(const char* call, DWORD error) const {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        initialized_ = false;
        logWarning() << "Serial: lost connection to " << devicePath_
                     << " (" << call << " failed, error " << error << ")";
    }
#elif !defined(__ANDROID__)
    // The device went away: close the fd and log once; later calls see
    // !isConnected(). err is the errno that showed it, 0 for a hangup.
    void markDisconnected(const char* call, int err) const {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
        initialized_ = false;
        logWarning() << "Serial: lost connection to " << devicePath_ << " ("
                     << call << (err ? ": " : "") << (err ? std::strerror(err) : "") << ")";
    }

    // errno values that mean the device is gone, not "try again"
    static bool isDeviceLostError(int err) {
        return err == EIO || err == ENXIO || err == ENODEV;
    }

    // Whether the tty was hung up (device removed, pty master closed). The
    // zero timeout never blocks. POLLNVAL is deliberately not counted. fd_ is
    // always open here, so Linux never reports it, and macOS poll() reports
    // it for any device it cannot attach a kqueue filter to, which would drop
    // a working port of such a driver. A macOS unplug does not need it: the
    // serial driver then fails ioctl(), read() and write() with ENXIO, and
    // every caller makes one of those calls before this one.
    bool isHungUp() const {
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) <= 0) return false;
        return (pfd.revents & (POLLHUP | POLLERR)) != 0;
    }

    // termios B-constant for a baud rate, or nullopt when there is none
    // (setup() then sets the exact rate with setSerialCustomBaudRate()).
    static std::optional<speed_t> baudRateToSpeed(int baudRate) {
        switch (baudRate) {
            case 300:    return B300;
            case 600:    return B600;
            case 1200:   return B1200;
            case 2400:   return B2400;
            case 4800:   return B4800;
            case 9600:   return B9600;
            case 19200:  return B19200;
            case 38400:  return B38400;
            case 57600:  return B57600;
            case 115200: return B115200;
            case 230400: return B230400;
#ifdef B460800
            case 460800: return B460800;
#endif
#ifdef B500000
            case 500000: return B500000;
#endif
#ifdef B576000
            case 576000: return B576000;
#endif
#ifdef B921600
            case 921600: return B921600;
#endif
#ifdef B1000000
            case 1000000: return B1000000;
#endif
#ifdef B1152000
            case 1152000: return B1152000;
#endif
#ifdef B1500000
            case 1500000: return B1500000;
#endif
#ifdef B2000000
            case 2000000: return B2000000;
#endif
#ifdef B2500000
            case 2500000: return B2500000;
#endif
#ifdef B3000000
            case 3000000: return B3000000;
#endif
#ifdef B3500000
            case 3500000: return B3500000;
#endif
#ifdef B4000000
            case 4000000: return B4000000;
#endif
            default:
                return std::nullopt;
        }
    }
#endif
};

} // namespace trussc

namespace tc = trussc;
