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
// readByte() and writeBytes() detect it, close the port, log one warning and
// fire onDisconnect, so isConnected() turns false and the app can call
// setup() again.
//
// Threads: a Serial has its own reader-writer lock. The I/O calls
// (available(), readBytes(), readByte(), writeBytes(), the flush calls,
// drain()) share it, so they never wait for each other. Opening and closing
// (setup(), close(), closing a lost port, the destructor, the moves) take it
// alone: they wait until the I/O calls in progress have returned, so no call
// ever uses a closed or reused port, and new I/O calls wait behind them.
// isConnected() and getDevicePath() take no part: they never wait for
// either kind of call. The lock is released before onDisconnect fires, so a
// listener may call setup(). Which thread reads or writes what, and in which
// order, is still up to the app.
// So a long I/O call (writeBytes() on Windows and Android, drain(); see
// writeBytes()) delays setup() / close() on another thread, not the other
// I/O calls. On Android, close(), the destructor and a move assignment also
// wait for the USB worker thread to stop (up to about 250 ms once
// connected). On Windows the system itself runs the calls on one port
// handle one at a time (it is opened without FILE_FLAG_OVERLAPPED), so
// available() / readBytes() / readByte() / the flush calls still wait there
// for a writeBytes() in progress.
//
// Logger listeners: Serial logs only with its lock released (what to log is
// decided under the lock, the line goes out after it), so a Logger listener
// that runs inline may call this Serial. One exception, on Android: the USB
// worker thread logs too (permission timeout, connected, lost connection,
// RX overflow, open errors), and close(), setup(), the destructor and a move
// wait for that thread to stop while they hold the lock. A listener running
// inline on the worker thread that then calls an I/O call, setup() or
// close() on this Serial may deadlock with them. isConnected() and
// getDevicePath() are safe there. Listen with Deliver::Main to be safe.
// =============================================================================

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <mutex>
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    #include <condition_variable>
#endif

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
#include "../events/tcEvent.h"

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

// ---------------------------------------------------------------------------
// Disconnect event
// ---------------------------------------------------------------------------

// Event args for Serial::onDisconnect
struct SerialDisconnectEventArgs {
    std::string portName;  // as passed to setup()
    int baudRate = 0;      // the rate the port was open at
    std::string reason;    // human-readable, e.g. "closed by close()", "read: Input/output error"
    bool wasClean = false; // true: closed by the app (close()); false: device lost / I/O error
};

namespace internal {
    // Log lines Serial makes while it holds its port lock, sent once it has
    // released it: a Logger listener that runs inline may call the same
    // Serial, and no thread may take that lock twice. Declare it before the
    // lock guard, so that it goes after the guard:
    //
    //   internal::SerialHeldLog held;
    //   Exclusive lock(lock_);
    //   held(LogLevel::Error) << "Serial: ...";
    class SerialHeldLog {
    public:
        // One line. Kept when it goes away, or sent at once with no
        // SerialHeldLog (the Android USB worker thread).
        struct Line {
            SerialHeldLog* held;
            LogLevel level;
            std::ostringstream text;

            template <typename T>
            Line& operator<<(const T& value) {
                text << value;
                return *this;
            }
            ~Line() {
                if (held) {
                    held->lines_.emplace_back(level, text.str());
                } else {
                    getLogger().log(level, text.str());
                }
            }
        };

        static Line line(SerialHeldLog* held, LogLevel level) { return Line{held, level, {}}; }
        Line operator()(LogLevel level) { return line(this, level); }

        SerialHeldLog() = default;
        SerialHeldLog(const SerialHeldLog&) = delete;
        SerialHeldLog& operator=(const SerialHeldLog&) = delete;
        ~SerialHeldLog() {
            for (const auto& l : lines_) getLogger().log(l.first, l.second);
        }

    private:
        std::vector<std::pair<LogLevel, std::string>> lines_;
    };
}

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
//
// The backend's worker thread is the one that finds a lost device. It only
// records the loss: Serial reports it (onDisconnect) from the next I/O call,
// close() or setup() on the app's thread, and isConnected() stays true until
// then, as it does on the other platforms until an I/O call finds the loss.
//
// Serial makes every call below with its lock held. setup(), close() and
// destroy() hold it exclusive, so they come one at a time and never overlap
// an I/O call. The I/O calls (available(), readBytes(), writeBytes(),
// flushInput(), isConnected(), isLost()) hold it shared, so they may come
// from several threads at once: the receive buffer has its own mutex, the
// state and loss flags are atomic, the connection fields they read (fd,
// endpoints) are written only while no I/O call runs or before the worker
// publishes Connected, and concurrent bulk transfers on one usbfs fd are
// each their own URB. The worker shares only the receive buffer and those
// flags with them.
// ---------------------------------------------------------------------------
namespace androidserial {
    struct Impl;
    // How close() found the connection, for Serial::onDisconnect
    enum class CloseResult { NotOpen, Closed, Lost };
    Impl* create();
    void destroy(Impl* impl);
    std::vector<SerialDeviceInfo> listDevices();
    // ended: how a connection that setup() had to close itself had ended
    // (NotOpen when Serial had closed it already), lostReason as for close()
    bool setup(Impl* impl, const std::string& devicePath, int baudRate,
               CloseResult& ended, std::string& lostReason);
    // Stop the worker and release the connection. Lost: the worker had found
    // the device gone, and lostReason is the text it logged.
    CloseResult close(Impl* impl, std::string& lostReason);
    // Connected, or lost with the loss not reported yet (see isLost())
    bool isConnected(const Impl* impl);
    // The worker found the device gone and close() has not collected it yet
    bool isLost(const Impl* impl);
    // A permission request for devicePath is still pending (setup() keeps it)
    bool isPendingFor(const Impl* impl, const std::string& devicePath);
    // The rate the backend opens (or will open) the device at
    int baudRate(const Impl* impl);
    // While alive, the backend's log lines on this thread go to held
    // instead of the Logger: Serial calls setup() / close() / destroy() with
    // its lock held, and sends them once it has let go. (The USB worker
    // thread logs at once.)
    class HoldLogs {
    public:
        explicit HoldLogs(internal::SerialHeldLog& held);
        ~HoldLogs();
        HoldLogs(const HoldLogs&) = delete;
        HoldLogs& operator=(const HoldLogs&) = delete;

    private:
        internal::SerialHeldLog* previous_;
    };
    int available(const Impl* impl);
    int readBytes(Impl* impl, void* buffer, int length);
    // error: the errno of a failed bulk transfer, 0 otherwise. The backend
    // does not log it: Serial does, once it has released its lock.
    int writeBytes(Impl* impl, const void* buffer, int length, int& error);
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

    // Read back the output rate an open serial fd runs at, as a number
    // (Linux: termios2). Returns false with errno set when it cannot, and
    // ENOTSUP where there is no such read-back (macOS). Also in tcSerial.cpp.
    bool readSerialBaudRate(int fd, int& baudRate);

    // Whether the rate a driver reports back counts as the requested one.
    // A Linux driver that cannot generate a rate does not fail tcsetattr() or
    // the termios2 ioctl: it writes back another rate instead, often the
    // previous one or 9600 (ftdi_sio above the chip maximum, cp210x clamping).
    // This happens for B-constant rates too. A driver's nearest divisor stays
    // close, so allow the 2% the kernel itself uses when it matches a rate to
    // a B-constant (tty_termios_encode_baud_rate).
    inline bool isBaudRateClose(int requested, int applied) {
        long long diff = static_cast<long long>(applied) - requested;
        long long allowed = requested / 50;
        return diff >= -allowed && diff <= allowed;
    }
}
#endif

namespace internal {
    // The write timeout Serial::setup() gives a Windows port (COMMTIMEOUTS):
    // WriteFile() gives up after multiplierMs per byte plus constantMs. It is
    // generous on purpose: a write that stalls a frame is easy to notice and
    // does little harm, while data that never goes out hurts more, and an
    // app that must not stall writes from a thread. So it allows 4 times the
    // time the bytes take on the wire at the port's rate, a byte being 10
    // bits (8N1 with its start and stop bits), rounded up to whole ms per
    // byte as COMMTIMEOUTS counts them, plus 5 s for a device that is merely
    // busy. A native USB (CDC) device ignores the rate, but a lower rate only
    // makes the timeout longer. Examples: 9600 baud gives 5 ms per byte (64 KB
    // in about 5.5 min), 40000 baud and up give 1 ms per byte (64 KB in about
    // 70 s). Here, not in the Windows branch, so the tests check it on every
    // platform.
    struct SerialWriteTimeout {
        unsigned long multiplierMs;  // per byte
        unsigned long constantMs;    // per WriteFile() call
    };

    inline SerialWriteTimeout serialWriteTimeout(int baudRate) {
        const unsigned long bitsPerByte = 10;
        const unsigned long slack = 4;
        const unsigned long constantMs = 5000;
        unsigned long baud = baudRate > 0 ? static_cast<unsigned long>(baudRate) : 1;
        unsigned long msPerByte = (bitsPerByte * 1000 * slack + baud - 1) / baud;  // rounded up
        return {msPerByte, constantMs};
    }
}

// ---------------------------------------------------------------------------
// Serial Communication Class
// ---------------------------------------------------------------------------

class TC_PLATFORMS("macos,windows,linux,android") Serial {
public:
    // -------------------------------------------------------------------------
    // Events
    //
    // onDisconnect fires once per open connection, when it ends:
    // - wasClean = false: available(), readBytes(), readByte() or writeBytes()
    //   found the device gone. reason is the text of the warning they log.
    // - wasClean = true: close() closed the open port (setup() calls it too,
    //   to close the previous port). reason is "closed by close()".
    // close() on a port that is not open does not fire, and neither does the
    // destructor or a move assignment over an open Serial, nor setup() when
    // it closes a port that a listener or another thread opened while
    // setup() was closing the previous one (it logs a warning instead).
    // Only those four I/O calls find a loss: a device that went away before
    // close() / setup() without one of them noticing ends with wasClean =
    // true and "closed by close()". On Android, where the worker thread may
    // have found it already, reason then goes on with "(the device had
    // already been lost: <reason>)", still with wasClean = true.
    //
    // THREADING: it fires inline on the thread that made that call, normally
    // the main thread, after the call has released the Serial's lock. On
    // Android the USB worker thread finds the loss; it only records it, and
    // the event fires from the next of those calls (or close() / setup()) on
    // the app's thread. A listener that must run on the main thread whichever
    // thread uses the Serial opts in:
    //
    //   listener = serial.onDisconnect.listen(fn, Deliver::Main);
    //
    // RECONNECTING: the port of the connection that ended is closed and the
    // lock is free before any listener runs, so a listener may call
    // setup(e.portName, e.baudRate). Check wasClean first, or every close()
    // reopens the port. The call that found the loss still returns its error
    // value and leaves the new connection alone. Another thread may call
    // setup() in the meantime, so an app that also connects from other
    // threads checks isConnected() / getDevicePath() in the listener before
    // it reconnects.
    //
    // Listeners stay with the Serial they were added to: a move does not
    // carry them over.
    // -------------------------------------------------------------------------
    // mutable: the const available() fires it when it finds the device gone
    mutable Event<SerialDisconnectEventArgs> onDisconnect;

#if defined(__ANDROID__)
    Serial() : aimpl_(androidserial::create()) {}
#else
    Serial() {}
#endif

    // Closes the port without firing onDisconnect: a listener that
    // reconnects must not run from a destructor.
    ~Serial() {
        internal::SerialHeldLog held;  // sent after the lock is released
        Exclusive lock(lock_);
#if defined(__ANDROID__)
        androidserial::Impl* old;
        {
            std::lock_guard<std::mutex> info(infoMutex_);
            old = aimpl_;
            aimpl_ = nullptr;
        }
        androidserial::HoldLogs hold(held);
        androidserial::destroy(old);  // closes the connection too
#else
        closePort(held);
#endif
    }

    // Non-copyable
    Serial(const Serial&) = delete;
    Serial& operator=(const Serial&) = delete;

    // Move-enabled. onDisconnect listeners are not moved (see Events above).
    Serial(Serial&& other) noexcept {
        Exclusive lock(other.lock_);
#if defined(_WIN32)
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
#elif !defined(__ANDROID__)
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        {
            std::lock_guard<std::mutex> info(other.infoMutex_);
#if defined(__ANDROID__)
            aimpl_ = other.aimpl_;
            other.aimpl_ = nullptr;
#endif
            devicePath_ = std::move(other.devicePath_);
            other.devicePath_.clear();
        }
        initialized_ = other.initialized_.load();
        baudRate_ = other.baudRate_;
        other.initialized_ = false;
        ++other.generation_;
    }

    Serial& operator=(Serial&& other) noexcept {
        if (this != &other) {
            internal::SerialHeldLog held;  // sent after the locks are released
            ExclusiveBoth locks(lock_, other.lock_);
            // The old port closes without onDisconnect, as in the destructor:
            // a listener that reconnected here would be overwritten below.
#if defined(_WIN32)
            closePort(held);
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
#elif !defined(__ANDROID__)
            closePort(held);
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
            // infoMutex_ one Serial at a time, and never across destroy(),
            // which waits for the old USB worker thread (see infoMutex_)
            std::string path;
#if defined(__ANDROID__)
            androidserial::Impl* moved;
            androidserial::Impl* old;
#endif
            {
                std::lock_guard<std::mutex> info(other.infoMutex_);
#if defined(__ANDROID__)
                moved = other.aimpl_;
                other.aimpl_ = nullptr;
#endif
                path = std::move(other.devicePath_);
                other.devicePath_.clear();
            }
            {
                std::lock_guard<std::mutex> info(infoMutex_);
#if defined(__ANDROID__)
                old = aimpl_;
                aimpl_ = moved;
#endif
                devicePath_ = std::move(path);
            }
#if defined(__ANDROID__)
            {
                androidserial::HoldLogs hold(held);
                androidserial::destroy(old);  // closes the old connection too
            }
#endif
            initialized_ = other.initialized_.load();
            baudRate_ = other.baudRate_;
            other.initialized_ = false;
            // Both hold another connection now (see closeLost())
            ++generation_;
            ++other.generation_;
#if defined(_WIN32)
            writeTimeoutWarned_ = false;
#endif
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

    // Connect by specifying device path. An open port is closed first, which
    // fires onDisconnect (wasClean = true).
    bool setup(const std::string& portName, int baudRate) {
#if defined(__ANDROID__)
        // End the previous connection first, so that onDisconnect reports it
        // with its own port and rate, and with the lock released. Not a
        // permission request still pending for this same device:
        // androidserial::setup() keeps that one alive (reconnect loops must
        // not re-trigger the permission dialog).
        PendingDisconnect closed;
        {
            internal::SerialHeldLog held;  // sent after the lock is released
            Exclusive lock(lock_);
            androidserial::HoldLogs hold(held);
            if (!aimpl_) {
                androidserial::Impl* created = androidserial::create();
                std::lock_guard<std::mutex> info(infoMutex_);
                aimpl_ = created;
            }
            if (!androidserial::isPendingFor(aimpl_, portName)) closeLocked(closed, held);
        }
        notifyDisconnect(closed);

        PendingDisconnect raced;
        bool ok = [&] {
            internal::SerialHeldLog held;  // sent after the lock is released
            Exclusive lock(lock_);
            androidserial::HoldLogs hold(held);
            ++generation_;
            // A listener that reconnected from that notification is overruled
            // by this call, which came first: androidserial::setup() closes
            // its connection.
            bool listenerReopened = closed && isOpenLocked();
            if (listenerReopened) {
                held(LogLevel::Warning) << "Serial: setup() closes the port an onDisconnect listener opened";
            }
            // Otherwise the backend closes a connection only when the pending
            // permission for this device came through after the check above.
            // That connection is reported with the values it was opened with.
            const std::string previousPath = devicePath_;
            const int previousRate = baudRate_;
            androidserial::CloseResult ended = androidserial::CloseResult::NotOpen;
            std::string lostReason;
            initialized_ = androidserial::setup(aimpl_, portName, baudRate, ended, lostReason);
            if (!listenerReopened) raced = closeArgs(ended, lostReason, previousPath, previousRate);
            {
                std::lock_guard<std::mutex> info(infoMutex_);
                devicePath_ = portName;
            }
            // The rate the backend opens at: a setup() again while the
            // permission for this device is pending keeps the first rate
            baudRate_ = androidserial::baudRate(aimpl_);
            return initialized_.load();
        }();
        notifyDisconnect(raced);
        return ok;
#else
        // The previous port closes first, and onDisconnect reports it with
        // the lock released
        close();
        internal::SerialHeldLog held;  // sent after the lock is released
        Exclusive lock(lock_);
        ++generation_;
        // A port opened since then, by a listener of that notification (or by
        // another thread), is overruled by this call, which came first. Close
        // it without another notification.
        if (closePort(held)) {
            held(LogLevel::Warning) << "Serial: setup() closes the port an onDisconnect listener opened";
        }
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
            held(LogLevel::Error) << "Serial: failed to open " << portName << " (error: " << GetLastError() << ")";
            return false;
        }

        // Timeouts: reads return at once with what has arrived. Writes wait
        // for the device, but give up after internal::serialWriteTimeout()
        // (see writeBytes()).
        internal::SerialWriteTimeout writeTimeout = internal::serialWriteTimeout(baudRate);
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = writeTimeout.multiplierMs;
        timeouts.WriteTotalTimeoutConstant = writeTimeout.constantMs;
        SetCommTimeouts(handle_, &timeouts);

        // Get DCB settings
        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle_, &dcb)) {
            held(LogLevel::Error) << "Serial: failed to get comm state";
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
            held(LogLevel::Error) << "Serial: failed to set comm state";
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // Clear buffers
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

        {
            std::lock_guard<std::mutex> info(infoMutex_);
            devicePath_ = portName;
        }
        baudRate_ = baudRate;
        initialized_ = true;
        writeTimeoutWarned_ = false;
        held(LogLevel::Notice) << "Serial: connected to " << portName << " at " << baudRate << " baud";
        return true;

#elif !defined(__ANDROID__)
        // POSIX (macOS, Linux)
        // Reject a nonsense rate before opening: opening asserts DTR, which
        // resets auto-reset boards such as most Arduinos.
        if (baudRate <= 0) {
            held(LogLevel::Error) << "Serial: invalid baud rate " << baudRate;
            return false;
        }

        // Open device (non-blocking)
        fd_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ == -1) {
            held(LogLevel::Error) << "Serial: failed to open " << portName;
            return false;
        }

        // Get exclusive lock
        if (ioctl(fd_, TIOCEXCL) == -1) {
            held(LogLevel::Error) << "Serial: failed to get exclusive access";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // Get terminal settings
        struct termios options;
        if (tcgetattr(fd_, &options) == -1) {
            held(LogLevel::Error) << "Serial: failed to get terminal attributes";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // The rate the port has before this setup() changes it (Linux
        // read-back), to recognize a driver that does not apply rates at all
        int previousBaudRate = 0;
        bool havePreviousRate = internal::readSerialBaudRate(fd_, previousBaudRate);

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
            held(LogLevel::Error) << "Serial: failed to set terminal attributes";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        int appliedBaudRate = baudRate;
        if (speed) {
            // On Linux tcsetattr() succeeds even when the driver swapped in
            // another rate, so read back what it applied. Where there is no
            // read-back (macOS), or it fails, the requested rate stands.
            int readBack = 0;
            if (internal::readSerialBaudRate(fd_, readBack)) appliedBaudRate = readBack;
        } else if (!internal::setSerialCustomBaudRate(fd_, baudRate, appliedBaudRate)) {
            int err = errno;
            held(LogLevel::Error) << "Serial: cannot set " << baudRate << " baud on " << portName
                       << " (" << std::strerror(err) << ")";
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        // A driver that could not generate the rate may have applied another
        // one (see isBaudRateClose()): that is a failure, not a connection at
        // the wrong speed. Unless the driver applies no rate at all.
        if (!internal::isBaudRateClose(baudRate, appliedBaudRate)) {
            if (havePreviousRate && appliedBaudRate == previousBaudRate &&
                driverIgnoresBaudRates(previousBaudRate)) {
                held(LogLevel::Warning) << "Serial: the driver of " << portName << " does not apply baud rates"
                             << " (it keeps " << previousBaudRate << "), so " << baudRate
                             << " has no effect there";
                appliedBaudRate = baudRate;
            } else {
                held(LogLevel::Error) << "Serial: cannot set " << baudRate << " baud on " << portName
                           << " (the driver applied " << appliedBaudRate << ")";
                ::close(fd_);
                fd_ = -1;
                return false;
            }
        }

        // Flush buffers
        tcflush(fd_, TCIOFLUSH);

        {
            std::lock_guard<std::mutex> info(infoMutex_);
            devicePath_ = portName;
        }
        baudRate_ = appliedBaudRate;
        initialized_ = true;
        if (appliedBaudRate != baudRate) {
            held(LogLevel::Warning) << "Serial: requested " << baudRate << " baud on " << portName
                         << ", the driver applied " << appliedBaudRate;
        }
        held(LogLevel::Notice) << "Serial: connected to " << portName << " at " << appliedBaudRate << " baud";
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

    // Disconnect. When the port was open, onDisconnect fires with
    // wasClean = true and reason "closed by close()" (see Events above for
    // a loss nobody had reported yet).
    void close() {
        PendingDisconnect closed;
        {
            internal::SerialHeldLog held;  // sent after the lock is released
            Exclusive lock(lock_);
            closeLocked(closed, held);
        }
        notifyDisconnect(closed);  // lock released: a listener may call setup() again
    }

    // ---------------------------------------------------------------------------
    // Status
    // ---------------------------------------------------------------------------

    // Whether the port is open and working. Turns false after close(), and
    // also when available() / readBytes() / readByte() / writeBytes() find
    // that the device went away (USB unplug, driver reset): the port is then
    // closed, onDisconnect fires, and setup() connects again. This call does
    // not probe the device itself; detection happens inside those I/O calls.
    // Android: may flip to true AFTER setup() returned false, once the user
    // grants USB permission, and stays true after the USB worker finds the
    // device gone until one of those calls reports it (see androidserial
    // note above).
    // It takes no part in the I/O lock: it never waits for setup(), close()
    // or an I/O call, and answers with the state at that moment. So a thread
    // that setup() / close() waits for (the Android USB worker, while one of
    // its log lines runs a Logger listener) may call it.
    bool isConnected() const {
#if defined(__ANDROID__)
        // The backend's state is atomic; infoMutex_ only keeps aimpl_ alive
        std::lock_guard<std::mutex> info(infoMutex_);
        return aimpl_ && androidserial::isConnected(aimpl_);
#else
        // Set true only once the port is open, and false before it closes
        return initialized_.load();
#endif
    }

    // Same as isConnected(). The older name, kept for existing code.
    bool isInitialized() const {
        return isConnected();
    }

    // Get current device path. A copy: another thread's setup() may change
    // it. Like isConnected(), it never waits for setup(), close() or an I/O
    // call.
    std::string getDevicePath() const {
        std::lock_guard<std::mutex> info(infoMutex_);
        return devicePath_;
    }

    // Get number of bytes available for reading.
    // Returns 0 when not connected; a device loss found here closes the port
    // (isConnected() turns false) and fires onDisconnect.
    int available() const {
        LossFound loss;
        int n = [&]() -> int {
            Shared lock(lock_);
#if defined(__ANDROID__)
            if (markLostOnWorker(loss)) return 0;
#endif
            if (!isOpenLocked()) return 0;

#if defined(_WIN32)
            COMSTAT comStat;
            DWORD errors;
            if (ClearCommError(handle_, &errors, &comStat)) {
                return (int)comStat.cbInQue;
            }
            markLost(loss, "ClearCommError", GetLastError());
            return 0;
#elif defined(__ANDROID__)
            return androidserial::available(aimpl_);
#else
            int bytesAvailable = 0;
            if (ioctl(fd_, FIONREAD, &bytesAvailable) == -1) {
                int err = errno;
                if (isDeviceLostError(err)) markLost(loss, "ioctl(FIONREAD)", err);
                return 0;
            }
            if (bytesAvailable > 0) return bytesAvailable;
            // Nothing buffered: tell a quiet port from a hung-up one
            if (isHungUp()) markLost(loss, "hangup", 0);
            return 0;
#endif
        }();
        closeLost(loss);
        return n;
    }

    // ---------------------------------------------------------------------------
    // Reading
    // ---------------------------------------------------------------------------

    // Read specified number of bytes
    // Returns: actual bytes read (>=0), -1 on error. A device loss found here
    // closes the port (isConnected() turns false), fires onDisconnect and
    // returns -1. Never blocks: it returns what has arrived so far.
    int readBytes(void* buffer, int length) {
        LossFound loss;
        int n = [&]() -> int {
            Shared lock(lock_);
#if defined(__ANDROID__)
            if (markLostOnWorker(loss)) return -1;
#endif
            if (!isOpenLocked()) return -1;
            if (length <= 0) return 0;

#if defined(_WIN32)
            DWORD bytesRead = 0;
            if (!ReadFile(handle_, buffer, length, &bytesRead, nullptr)) {
                markLost(loss, "ReadFile", GetLastError());
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
                    if (isDeviceLostError(err)) markLost(loss, "read", err);
                    return -1;
                }
            }
            // No data. With VMIN = VTIME = 0, read() returns 0 both for a quiet
            // port and for a hung-up tty, so only poll() can tell them apart.
            if (isHungUp()) {
                markLost(loss, "hangup", 0);
                return -1;
            }
            return 0;
#endif
        }();
        closeLost(loss);
        return n;
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
    // found here closes the port (isConnected() turns false), fires
    // onDisconnect and returns -2.
    int readByte() {
        LossFound loss;
        int n = [&]() -> int {
            Shared lock(lock_);
#if defined(__ANDROID__)
            if (markLostOnWorker(loss)) return -2;
#endif
            if (!isOpenLocked()) return -2;

            unsigned char byte;
#if defined(_WIN32)
            DWORD bytesRead = 0;
            if (!ReadFile(handle_, &byte, 1, &bytesRead, nullptr)) {
                markLost(loss, "ReadFile", GetLastError());
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
                    if (isDeviceLostError(err)) markLost(loss, "read", err);
                    return -2;  // Error
                }
            }
            // No data, or a hung-up tty (see readBytes())
            if (isHungUp()) {
                markLost(loss, "hangup", 0);
                return -2;
            }
            return -1;  // No data
#endif
        }();
        closeLost(loss);
        return n;
    }

    // ---------------------------------------------------------------------------
    // Writing
    // ---------------------------------------------------------------------------

    // Write specified number of bytes
    // Returns: actual bytes written, -1 on error. A device loss found here
    // closes the port (isConnected() turns false), fires onDisconnect and
    // returns -1. How long it may take depends on the platform:
    // - macOS / Linux: never blocks (the port is O_NONBLOCK). It may write
    //   fewer bytes than asked, or return -1 while the output buffer is full.
    // - Windows: waits until the driver has taken every byte, at most
    //   internal::serialWriteTimeout(): 4 times the time the bytes take at the
    //   port's rate, plus 5 s. On a timeout it returns the bytes written so
    //   far (possibly 0), logs a warning once per connection, and the port
    //   stays open.
    // - Android: waits up to 1 s per 16 KB for the device to take the data.
    // A write that must never stall the app belongs on a thread of its own:
    // the other I/O calls do not wait for it (on Windows available() and the
    // reads still do; see the top of this file).
    int writeBytes(const void* buffer, int length) {
        LossFound loss;
#if defined(_WIN32)
        bool warnTimeout = false;  // decided under the lock, logged after it
        std::string port;
#elif defined(__ANDROID__)
        int writeError = 0;
#endif
        int n = [&]() -> int {
            Shared lock(lock_);
#if defined(__ANDROID__)
            if (markLostOnWorker(loss)) return -1;
#endif
            if (!isOpenLocked()) return -1;
            if (length <= 0) return 0;

#if defined(_WIN32)
            DWORD bytesWritten = 0;
            if (!WriteFile(handle_, buffer, length, &bytesWritten, nullptr)) {
                DWORD error = GetLastError();
                // A driver may report the write timeout as a failure too
                if (error == ERROR_SEM_TIMEOUT || error == ERROR_TIMEOUT) {
                    warnTimeout = firstWriteTimeout(port);
                    return (int)bytesWritten;
                }
                markLost(loss, "WriteFile", error);
                return -1;
            }
            // The write timeout ends the call with part of the data written
            if ((int)bytesWritten < length) warnTimeout = firstWriteTimeout(port);
            return (int)bytesWritten;
#elif defined(__ANDROID__)
            return androidserial::writeBytes(aimpl_, buffer, length, writeError);
#else
            ssize_t result = write(fd_, buffer, length);
            if (result == -1) {
                int err = errno;
                if (isDeviceLostError(err)) markLost(loss, "write", err);
                return -1;
            }
            return static_cast<int>(result);
#endif
        }();
        // Logged here, with the lock released: a Logger listener may use
        // this Serial
#if defined(_WIN32)
        if (warnTimeout) {
            logWarning() << "Serial: a write to " << port << " timed out (" << n << " of "
                         << length << " bytes sent); writeBytes() returns what was sent. Write"
                         << " from a thread of its own if the device takes data slowly";
        }
#elif defined(__ANDROID__)
        if (writeError != 0) {
            logError() << "Serial: write failed (" << std::strerror(writeError) << ")";
        }
#endif
        closeLost(loss);
        return n;
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
        Shared lock(lock_);
        if (!isOpenLocked()) return;
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
        Shared lock(lock_);
        if (!isOpenLocked()) return;
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
        Shared lock(lock_);
        if (!isOpenLocked()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
#elif defined(__ANDROID__)
        androidserial::flushInput(aimpl_);
#else
        tcflush(fd_, TCIOFLUSH);
#endif
    }

    // Wait until output completes. setup() / close() on another thread wait
    // for it meanwhile.
    void drain() {
        Shared lock(lock_);
        if (!isOpenLocked()) return;
#if defined(_WIN32)
        FlushFileBuffers(handle_);
#elif defined(__ANDROID__)
        // No-op: Android writes are synchronous bulk transfers
#else
        tcdrain(fd_);
#endif
    }

private:
    // The port state below is guarded by lock_: the I/O calls read it with
    // the lock shared, and only opening and closing change it, with the lock
    // exclusive. mutable: the const available() closes the port when it
    // finds the device gone (see closeLost()).
#if defined(_WIN32)
    mutable HANDLE handle_ = INVALID_HANDLE_VALUE;  // Windows handle
#elif defined(__ANDROID__)
    androidserial::Impl* aimpl_ = nullptr;           // Android backend state
#else
    mutable int fd_ = -1;                            // File descriptor (POSIX)
#endif
    // Connection state. Atomic: isConnected() reads it without the lock,
    // so it is set true only once the port is open and false before it
    // closes (not used for that on Android, whose backend state is atomic).
    mutable std::atomic<bool> initialized_{false};
    // Current device path. Written with the lock exclusive and infoMutex_;
    // getDevicePath() reads it with infoMutex_ alone (see below).
    std::string devicePath_;
    int baudRate_ = 0;                  // Rate the current port was opened at
    // Counts the connections: setup() and the moves bump it, so a loss found
    // on one connection never closes the next one (see closeLost())
    std::uint64_t generation_ = 0;
#if defined(_WIN32)
    // Set once a write of this connection timed out (see writeBytes()).
    // Tested and set with the lock shared (several writers may time out at
    // once), reset by setup() and the moves with it exclusive.
    std::atomic<bool> writeTimeoutWarned_{false};

    // With the lock shared: whether this is the connection's first write
    // timeout, the one writeBytes() warns about. Sets port for the warning.
    bool firstWriteTimeout(std::string& port) {
        if (writeTimeoutWarned_.exchange(true)) return false;
        port = devicePath_;
        return true;
    }
#endif

    // A reader-writer lock with two rules:
    // 1. The I/O calls share it.
    // 2. Opening and closing take it alone: they wait until no I/O call is
    //    left, and while they wait, new I/O calls wait behind them, so I/O
    //    calls that keep coming cannot starve a close().
    // std::shared_mutex does not promise rule 2 (libstdc++ uses glibc's
    // reader-preferring pthread_rwlock), so Serial has its own. No thread
    // takes it twice: the public calls never call each other with it held
    // (the *Locked() helpers expect it held), and nothing that could call
    // back into Serial runs under it (log lines wait in SerialHeldLog,
    // onDisconnect fires after it).
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    struct PortLock {  // no threads: nothing to wait for
        void lock() {}
        void unlock() {}
        void lock_shared() {}
        void unlock_shared() {}
    };
#else
    struct PortLock {
        std::mutex m;
        std::condition_variable entry;    // new callers, while a writer is in or waits
        std::condition_variable drained;  // the writer, for the readers to leave
        bool writer = false;              // a writer holds it or waits for readers
        unsigned readers = 0;

        void lock() {
            std::unique_lock<std::mutex> g(m);
            entry.wait(g, [this] { return !writer; });
            writer = true;
            drained.wait(g, [this] { return readers == 0; });
        }
        void unlock() {
            {
                std::lock_guard<std::mutex> g(m);
                writer = false;
            }
            entry.notify_all();
        }
        void lock_shared() {
            std::unique_lock<std::mutex> g(m);
            entry.wait(g, [this] { return !writer; });
            ++readers;
        }
        void unlock_shared() {
            bool last;
            {
                std::lock_guard<std::mutex> g(m);
                last = --readers == 0 && writer;
            }
            if (last) drained.notify_one();
        }
    };
#endif
    mutable PortLock lock_;

    // Guards what getDevicePath() and (Android) isConnected() read without
    // lock_: devicePath_, and the aimpl_ pointer. Taken briefly, always
    // after lock_ when both are held, never across I/O, a lock_ acquisition
    // or androidserial::destroy() (which waits for the USB worker). So those
    // two calls never wait for setup() / close(): a thread that close() is
    // waiting for may make them.
    mutable std::mutex infoMutex_;

    // For the I/O calls
    struct Shared {
        PortLock& l;
        explicit Shared(PortLock& lock) : l(lock) { l.lock_shared(); }
        ~Shared() { l.unlock_shared(); }
    };
    // For opening and closing
    struct Exclusive {
        PortLock& l;
        explicit Exclusive(PortLock& lock) : l(lock) { l.lock(); }
        ~Exclusive() { l.unlock(); }
    };
    // Two Serials in one order (by address), for a move assignment, so that
    // two threads moving them into each other cannot deadlock
    struct ExclusiveBoth {
        PortLock& first;
        PortLock& second;
        ExclusiveBoth(PortLock& a, PortLock& b)
            : first(std::less<PortLock*>()(&a, &b) ? a : b),
              second(std::less<PortLock*>()(&a, &b) ? b : a) {
            first.lock();
            second.lock();
        }
        ~ExclusiveBoth() {
            second.unlock();
            first.unlock();
        }
    };

    // isConnected() with the lock held
    bool isOpenLocked() const {
#if defined(_WIN32)
        return initialized_ && handle_ != INVALID_HANDLE_VALUE;
#elif defined(__ANDROID__)
        return aimpl_ && androidserial::isConnected(aimpl_);
#else
        return initialized_ && fd_ != -1;
#endif
    }

    // A disconnect found while the lock was held, to fire once it is released
    using PendingDisconnect = std::optional<SerialDisconnectEventArgs>;

    // The args for the current port (with the lock held)
    SerialDisconnectEventArgs disconnectArgs(std::string reason, bool wasClean) const {
        SerialDisconnectEventArgs args;
        args.portName = devicePath_;
        args.baudRate = baudRate_;
        args.reason = std::move(reason);
        args.wasClean = wasClean;
        return args;
    }

    // Fire onDisconnect for a disconnect found under the lock. The caller has
    // closed the port, cleared its state and released the lock by now, so a
    // listener may call setup() to reconnect, from this thread or another.
    // The caller must return right after this and not touch the port: it may
    // be a new one.
    void notifyDisconnect(PendingDisconnect& pending) const {
        if (pending) onDisconnect.notify(*pending);
    }

    // A device loss an I/O call found with the lock shared. The call closes
    // the port afterwards, in closeLost(), with the lock exclusive.
    struct LossFound {
        bool found = false;
        std::uint64_t generation = 0;  // the connection it was found on
        std::string reason;            // the warning's text (Android: from close())
    };

    // After an I/O call found the device gone: close the port, log once (the
    // Android worker has logged already) and fire onDisconnect. Only if it is
    // still the connection the call used: another thread may have closed it,
    // or closed and reopened it, since the call let go of the shared lock, and
    // two calls may have found the same loss. Taking the lock exclusive waits
    // for the other I/O calls in progress, so none of them uses the port
    // after it is closed. Logs and fires with the lock released.
    void closeLost(LossFound& loss) const {
        if (!loss.found) return;
        PendingDisconnect lost;
        std::string port;
        {
            internal::SerialHeldLog held;  // sent after the lock is released
            Exclusive lock(lock_);
            if (generation_ != loss.generation || !isOpenLocked()) return;
            initialized_ = false;  // isConnected() reads it without the lock
#if defined(_WIN32)
            // With some drivers a stale handle keeps the port from being
            // reopened (usbser.sys does not)
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
#elif defined(__ANDROID__)
            androidserial::HoldLogs hold(held);
            androidserial::close(aimpl_, loss.reason);
#else
            ::close(fd_);
            fd_ = -1;
#endif
            port = devicePath_;
            lost = disconnectArgs(loss.reason, false);
        }
#if !defined(__ANDROID__)
        logWarning() << "Serial: lost connection to " << port << " (" << loss.reason << ")";
#endif
        notifyDisconnect(lost);
    }

    // close() with the lock held exclusive: closes the port and sets `closed`
    // to the notification to fire once the lock is released. Its log lines
    // go to held.
    void closeLocked(PendingDisconnect& closed, internal::SerialHeldLog& held) {
#if defined(__ANDROID__)
        androidserial::HoldLogs hold(held);
        std::string lostReason;
        androidserial::CloseResult ended = aimpl_ ? androidserial::close(aimpl_, lostReason)
                                                  : androidserial::CloseResult::NotOpen;
        initialized_ = false;
        closed = closeArgs(ended, lostReason, devicePath_, baudRate_);
#else
        if (closePort(held)) closed = disconnectArgs("closed by close()", true);
#endif
    }

#if defined(__ANDROID__)
    // The notification for a connection closed on the app's request. The app
    // ended it, so wasClean is true even when the worker had found the device
    // gone and no call had reported that yet; the reason then says so. (The
    // other platforms find a loss only inside the I/O calls, so there a
    // close() after an unnoticed unplug is a plain "closed by close()".)
    static PendingDisconnect closeArgs(androidserial::CloseResult ended, const std::string& lostReason,
                                       const std::string& portName, int baudRate) {
        if (ended == androidserial::CloseResult::NotOpen) return std::nullopt;
        SerialDisconnectEventArgs args;
        args.portName = portName;
        args.baudRate = baudRate;
        args.reason = "closed by close()";
        if (ended == androidserial::CloseResult::Lost) {
            args.reason += " (the device had already been lost: " + lostReason + ")";
        }
        args.wasClean = true;
        return args;
    }

    // With the lock shared: the USB worker found the device gone and only
    // recorded it. Returns true when it did; the caller returns its error
    // value, and closeLost() collects the loss from the backend.
    bool markLostOnWorker(LossFound& loss) const {
        if (!aimpl_ || !androidserial::isLost(aimpl_)) return false;
        loss.found = true;
        loss.generation = generation_;
        return true;
    }
#else
    // Close the port without firing onDisconnect (the destructor, a move
    // assignment, and close() before it notifies). Returns whether it was
    // open. With the lock held exclusive; its log line goes to held.
    bool closePort(internal::SerialHeldLog& held) {
        initialized_ = false;  // isConnected() reads it without the lock
#if defined(_WIN32)
        bool wasOpen = handle_ != INVALID_HANDLE_VALUE;
        if (wasOpen) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            held(LogLevel::Verbose) << "Serial: disconnected from " << devicePath_;
        }
#else
        bool wasOpen = fd_ != -1;
        if (wasOpen) {
            ::close(fd_);
            fd_ = -1;
            held(LogLevel::Verbose) << "Serial: disconnected from " << devicePath_;
        }
#endif
        return wasOpen;
    }
#endif

#if defined(_WIN32)
    // With the lock shared: record a loss for closeLost(). Any failure of
    // ClearCommError / ReadFile / WriteFile counts as one: with fAbortOnError
    // off, line errors do not fail them, and the error code a removed device
    // returns differs between drivers.
    void markLost(LossFound& loss, const char* call, DWORD error) const {
        loss.found = true;
        loss.generation = generation_;
        loss.reason = std::string(call) + " failed, error " + std::to_string(error);
    }
#elif !defined(__ANDROID__)
    // With the lock shared: record a loss for closeLost(). err is the errno
    // that showed it, 0 for a hangup.
    void markLost(LossFound& loss, const char* call, int err) const {
        loss.found = true;
        loss.generation = generation_;
        loss.reason = call;
        if (err) {
            loss.reason += ": ";
            loss.reason += std::strerror(err);
        }
    }

    // Whether the driver ignores rate changes altogether. The kernel then
    // restores the old rate after every change: a driver without
    // set_termios (u_serial /dev/ttyGS*, usb_serial_generic,
    // usb-serial-simple, xHCI DbC). A driver that rejected just the
    // requested rate, and kept the old one, still applies another standard
    // rate, so try one once (termios2 takes any rate, B-constant ones too).
    // With the lock held exclusive, while setup() opens the port.
    bool driverIgnoresBaudRates(int keptRate) const {
        int probe = keptRate == 9600 ? 19200 : 9600;
        int probeApplied = 0;
        if (!internal::setSerialCustomBaudRate(fd_, probe, probeApplied)) return false;
        return probeApplied == keptRate;
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
