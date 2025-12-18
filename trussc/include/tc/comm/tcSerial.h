#pragma once

// =============================================================================
// TrussC Serial Communication
// クロスプラットフォーム対応のシリアル通信クラス
// - Windows: Win32 API (CreateFile, SetCommState, etc.)
// - macOS/Linux: POSIX API (termios)
// =============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

// プラットフォーム固有ヘッダー
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
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
// シリアルデバイス情報
// ---------------------------------------------------------------------------

struct SerialDeviceInfo {
    int deviceId;           // デバイスインデックス
    std::string devicePath; // デバイスパス（例: COM3, /dev/tty.usbserial-A10172HG）
    std::string deviceName; // デバイス名

    int getDeviceID() const { return deviceId; }
    const std::string& getDevicePath() const { return devicePath; }
    const std::string& getDeviceName() const { return deviceName; }
};

// ---------------------------------------------------------------------------
// シリアル通信クラス
// ---------------------------------------------------------------------------

class Serial {
public:
#if defined(_WIN32)
    Serial() : handle_(INVALID_HANDLE_VALUE), initialized_(false) {}
#else
    Serial() : fd_(-1), initialized_(false) {}
#endif

    ~Serial() {
        close();
    }

    // コピー禁止
    Serial(const Serial&) = delete;
    Serial& operator=(const Serial&) = delete;

    // ムーブ可能
    Serial(Serial&& other) noexcept
#if defined(_WIN32)
        : handle_(other.handle_), initialized_(other.initialized_), devicePath_(std::move(other.devicePath_)) {
        other.handle_ = INVALID_HANDLE_VALUE;
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
    // デバイス列挙
    // ---------------------------------------------------------------------------

    // 利用可能なシリアルデバイスをコンソールに表示
    void listDevices() {
        auto devices = getDeviceList();
        tcLogNotice() << "Serial devices:";
        for (const auto& dev : devices) {
            tcLogNotice() << "  [" << dev.deviceId << "] " << dev.devicePath;
        }
    }

    // 利用可能なシリアルデバイスの一覧を取得
    std::vector<SerialDeviceInfo> getDeviceList() {
        std::vector<SerialDeviceInfo> devices;

#if defined(_WIN32)
        // Windows: COM1〜COM256 を試す
        int id = 0;
        for (int i = 1; i <= 256; i++) {
            std::string portName = "COM" + std::to_string(i);
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
        // macOS: /dev/tty.* と /dev/cu.* を列挙
        DIR* dir = opendir("/dev");
        if (dir) {
            struct dirent* entry;
            int id = 0;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                // tty.usbserial, tty.usbmodem, cu.* などをフィルタ
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
#elif defined(__linux__)
        // Linux: /dev/ttyUSB*, /dev/ttyACM* を列挙
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

    // ---------------------------------------------------------------------------
    // 接続
    // ---------------------------------------------------------------------------

    // デバイスパスを指定して接続
    bool setup(const std::string& portName, int baudRate) {
        close();

#if defined(_WIN32)
        // Windows: COM ポートを開く
        std::string fullPath = portName;
        // COM10以上の場合は \\.\COMxx 形式にする
        if (portName.find("\\\\.\\") != 0) {
            fullPath = "\\\\.\\" + portName;
        }

        handle_ = CreateFileA(fullPath.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0,                     // 排他アクセス
                              nullptr,               // セキュリティ属性なし
                              OPEN_EXISTING,
                              0,                     // 非オーバーラップモード
                              nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            tcLogError() << "Serial: failed to open " << portName << " (error: " << GetLastError() << ")";
            return false;
        }

        // タイムアウト設定（非ブロッキングに近い動作）
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
        SetCommTimeouts(handle_, &timeouts);

        // DCB 設定を取得
        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle_, &dcb)) {
            tcLogError() << "Serial: failed to get comm state";
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // ボーレートを設定
        dcb.BaudRate = baudRate;
        dcb.ByteSize = 8;               // 8データビット
        dcb.Parity = NOPARITY;          // パリティなし
        dcb.StopBits = ONESTOPBIT;      // 1ストップビット
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;       // CTSフロー制御無効
        dcb.fOutxDsrFlow = FALSE;       // DSRフロー制御無効
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = FALSE;              // XON/XOFF 出力無効
        dcb.fInX = FALSE;               // XON/XOFF 入力無効
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fAbortOnError = FALSE;

        if (!SetCommState(handle_, &dcb)) {
            tcLogError() << "Serial: failed to set comm state";
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // バッファをクリア
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

        devicePath_ = portName;
        initialized_ = true;
        tcLogNotice() << "Serial: connected to " << portName << " at " << baudRate << " baud";
        return true;

#else
        // POSIX (macOS, Linux)
        // デバイスを開く（非ブロッキング）
        fd_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ == -1) {
            tcLogError() << "Serial: failed to open " << portName;
            return false;
        }

        // 排他ロックを取得
        if (ioctl(fd_, TIOCEXCL) == -1) {
            tcLogError() << "Serial: failed to get exclusive access";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // 端末設定を取得
        struct termios options;
        if (tcgetattr(fd_, &options) == -1) {
            tcLogError() << "Serial: failed to get terminal attributes";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // Raw モードに設定（入出力処理なし）
        cfmakeraw(&options);

        // ボーレートを設定
        speed_t speed = baudRateToSpeed(baudRate);
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);

        // 8N1 (8データビット、パリティなし、1ストップビット)
        options.c_cflag &= ~PARENB;  // パリティなし
        options.c_cflag &= ~CSTOPB;  // 1ストップビット
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;       // 8データビット

        // ローカル接続、受信有効
        options.c_cflag |= (CLOCAL | CREAD);

        // ハードウェアフロー制御を無効
        options.c_cflag &= ~CRTSCTS;

        // ソフトウェアフロー制御を無効
        options.c_iflag &= ~(IXON | IXOFF | IXANY);

        // 最小受信文字数と待機時間
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;

        // 設定を適用
        if (tcsetattr(fd_, TCSANOW, &options) == -1) {
            tcLogError() << "Serial: failed to set terminal attributes";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // バッファをフラッシュ
        tcflush(fd_, TCIOFLUSH);

        devicePath_ = portName;
        initialized_ = true;
        tcLogNotice() << "Serial: connected to " << portName << " at " << baudRate << " baud";
        return true;
#endif
    }

    // デバイスインデックスを指定して接続
    bool setup(int deviceIndex, int baudRate) {
        auto devices = getDeviceList();
        if (deviceIndex < 0 || deviceIndex >= (int)devices.size()) {
            tcLogError() << "Serial: device index " << deviceIndex << " out of range (0-" << (int)devices.size() - 1 << ")";
            return false;
        }
        return setup(devices[deviceIndex].devicePath, baudRate);
    }

    // 切断
    void close() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            tcLogVerbose() << "Serial: disconnected from " << devicePath_;
        }
#else
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
            tcLogVerbose() << "Serial: disconnected from " << devicePath_;
        }
#endif
        initialized_ = false;
    }

    // ---------------------------------------------------------------------------
    // 状態確認
    // ---------------------------------------------------------------------------

    // 接続中かどうか
    bool isInitialized() const {
#if defined(_WIN32)
        return initialized_ && handle_ != INVALID_HANDLE_VALUE;
#else
        return initialized_ && fd_ != -1;
#endif
    }

    // 接続中のデバイスパスを取得
    const std::string& getDevicePath() const {
        return devicePath_;
    }

    // 読み取り可能なバイト数を取得
    int available() const {
        if (!isInitialized()) return 0;

#if defined(_WIN32)
        COMSTAT comStat;
        DWORD errors;
        if (ClearCommError(handle_, &errors, &comStat)) {
            return (int)comStat.cbInQue;
        }
        return 0;
#else
        int bytesAvailable = 0;
        if (ioctl(fd_, FIONREAD, &bytesAvailable) == -1) {
            return 0;
        }
        return bytesAvailable;
#endif
    }

    // ---------------------------------------------------------------------------
    // 読み取り
    // ---------------------------------------------------------------------------

    // 指定バイト数を読み取り
    // 戻り値: 実際に読み取ったバイト数（0以上）、エラー時は-1
    int readBytes(void* buffer, int length) {
        if (!isInitialized()) return -1;
        if (length <= 0) return 0;

#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, buffer, length, &bytesRead, nullptr)) {
            return -1;
        }
        return (int)bytesRead;
#else
        ssize_t result = read(fd_, buffer, length);
        if (result == -1) {
            // EAGAIN/EWOULDBLOCK は「データがない」状態なので0を返す
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        return static_cast<int>(result);
#endif
    }

    // std::string に読み取り（便利メソッド）
    int readBytes(std::string& buffer, int length) {
        buffer.resize(length);
        int result = readBytes(buffer.data(), length);
        if (result >= 0) {
            buffer.resize(result);
        }
        return result;
    }

    // 1バイト読み取り
    // 戻り値: 読み取ったバイト（0-255）、データがない場合は-1、エラー時は-2
    int readByte() {
        if (!isInitialized()) return -2;

        unsigned char byte;
#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, &byte, 1, &bytesRead, nullptr)) {
            return -2;  // エラー
        }
        if (bytesRead == 1) {
            return byte;
        }
        return -1;  // データがない
#else
        ssize_t result = read(fd_, &byte, 1);
        if (result == 1) {
            return byte;
        } else if (result == 0 || (result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            return -1;  // データがない
        }
        return -2;  // エラー
#endif
    }

    // ---------------------------------------------------------------------------
    // 書き込み
    // ---------------------------------------------------------------------------

    // 指定バイト数を書き込み
    // 戻り値: 実際に書き込んだバイト数、エラー時は-1
    int writeBytes(const void* buffer, int length) {
        if (!isInitialized()) return -1;
        if (length <= 0) return 0;

#if defined(_WIN32)
        DWORD bytesWritten = 0;
        if (!WriteFile(handle_, buffer, length, &bytesWritten, nullptr)) {
            return -1;
        }
        return (int)bytesWritten;
#else
        ssize_t result = write(fd_, buffer, length);
        if (result == -1) {
            return -1;
        }
        return static_cast<int>(result);
#endif
    }

    // std::string を書き込み
    int writeBytes(const std::string& buffer) {
        return writeBytes(buffer.data(), static_cast<int>(buffer.size()));
    }

    // 1バイト書き込み
    bool writeByte(unsigned char byte) {
        return writeBytes(&byte, 1) == 1;
    }

    // ---------------------------------------------------------------------------
    // バッファ制御
    // ---------------------------------------------------------------------------

    // 入力バッファをフラッシュ
    void flushInput() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_RXCLEAR);
#else
        tcflush(fd_, TCIFLUSH);
#endif
    }

    // 出力バッファをフラッシュ
    void flushOutput() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_TXCLEAR);
#else
        tcflush(fd_, TCOFLUSH);
#endif
    }

    // 入出力バッファをフラッシュ
    void flush() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
        tcflush(fd_, TCIOFLUSH);
#endif
    }

    // 出力が完了するまで待機
    void drain() {
        if (!isInitialized()) return;
#if defined(_WIN32)
        FlushFileBuffers(handle_);
#else
        tcdrain(fd_);
#endif
    }

private:
#if defined(_WIN32)
    HANDLE handle_;        // Windows ハンドル
#else
    int fd_;               // ファイルディスクリプタ (POSIX)
#endif
    bool initialized_;     // 接続状態
    std::string devicePath_; // 接続中のデバイスパス

#if !defined(_WIN32)
    // ボーレートを speed_t に変換 (POSIX only)
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
                tcLogWarning() << "Serial: unsupported baud rate " << baudRate << ", using 9600";
                return B9600;
        }
    }
#endif
};

} // namespace trussc

namespace tc = trussc;
