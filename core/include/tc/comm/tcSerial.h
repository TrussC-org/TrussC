#pragma once

// =============================================================================
// TrussC Serial Communication
// Cross-platform serial communication class
// - Windows: Win32 API (CreateFile, SetCommState, etc.)
// - macOS/Linux: POSIX API (termios)
// - Android: USB Host API (CDC-ACM devices only), JNI + usbfs.
//   See platform/android/tcSerial_android.cpp for details and caveats.
// =============================================================================

#include <string>
#include <vector>
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

// ---------------------------------------------------------------------------
// Serial Communication Class
// ---------------------------------------------------------------------------

class Serial {
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

        // Set baud rate
        speed_t speed = baudRateToSpeed(baudRate);
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);

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

        // Flush buffers
        tcflush(fd_, TCIOFLUSH);

        devicePath_ = portName;
        initialized_ = true;
        logNotice() << "Serial: connected to " << portName << " at " << baudRate << " baud";
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

    // Check if connected
    // Android: may flip to true AFTER setup() returned false, once the user
    // grants USB permission (see androidserial note above).
    bool isInitialized() const {
#if defined(_WIN32)
        return initialized_ && handle_ != INVALID_HANDLE_VALUE;
#elif defined(__ANDROID__)
        return aimpl_ && androidserial::isConnected(aimpl_);
#else
        return initialized_ && fd_ != -1;
#endif
    }

    // Get current device path
    const std::string& getDevicePath() const {
        return devicePath_;
    }

    // Get number of bytes available for reading
    int available() const {
        if (!isInitialized()) return 0;

#if defined(_WIN32)
        COMSTAT comStat;
        DWORD errors;
        if (ClearCommError(handle_, &errors, &comStat)) {
            return (int)comStat.cbInQue;
        }
        return 0;
#elif defined(__ANDROID__)
        return androidserial::available(aimpl_);
#else
        int bytesAvailable = 0;
        if (ioctl(fd_, FIONREAD, &bytesAvailable) == -1) {
            return 0;
        }
        return bytesAvailable;
#endif
    }

    // ---------------------------------------------------------------------------
    // Reading
    // ---------------------------------------------------------------------------

    // Read specified number of bytes
    // Returns: actual bytes read (>=0), -1 on error
    int readBytes(void* buffer, int length) {
        if (!isInitialized()) return -1;
        if (length <= 0) return 0;

#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, buffer, length, &bytesRead, nullptr)) {
            return -1;
        }
        return (int)bytesRead;
#elif defined(__ANDROID__)
        return androidserial::readBytes(aimpl_, buffer, length);
#else
        ssize_t result = read(fd_, buffer, length);
        if (result == -1) {
            // EAGAIN/EWOULDBLOCK means "no data available", return 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        return static_cast<int>(result);
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
    // Returns: byte read (0-255), -1 if no data, -2 on error
    int readByte() {
        if (!isInitialized()) return -2;

        unsigned char byte;
#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, &byte, 1, &bytesRead, nullptr)) {
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
        if (result == 1) {
            return byte;
        } else if (result == 0 || (result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            return -1;  // No data
        }
        return -2;  // Error
#endif
    }

    // ---------------------------------------------------------------------------
    // Writing
    // ---------------------------------------------------------------------------

    // Write specified number of bytes
    // Returns: actual bytes written, -1 on error
    int writeBytes(const void* buffer, int length) {
        if (!isInitialized()) return -1;
        if (length <= 0) return 0;

#if defined(_WIN32)
        DWORD bytesWritten = 0;
        if (!WriteFile(handle_, buffer, length, &bytesWritten, nullptr)) {
            return -1;
        }
        return (int)bytesWritten;
#elif defined(__ANDROID__)
        return androidserial::writeBytes(aimpl_, buffer, length);
#else
        ssize_t result = write(fd_, buffer, length);
        if (result == -1) {
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
#if defined(_WIN32)
    HANDLE handle_;        // Windows handle
#elif defined(__ANDROID__)
    androidserial::Impl* aimpl_;  // Android backend state
#else
    int fd_;               // File descriptor (POSIX)
#endif
    bool initialized_;     // Connection state
    std::string devicePath_; // Current device path

#if !defined(_WIN32) && !defined(__ANDROID__)
    // Convert baud rate to speed_t (POSIX only)
    speed_t baudRateToSpeed(int baudRate) {
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
#ifdef B921600
            case 921600: return B921600;
#endif
            default:
                logWarning() << "Serial: unsupported baud rate " << baudRate << ", using 9600";
                return B9600;
        }
    }
#endif
};

} // namespace trussc

namespace tc = trussc;
