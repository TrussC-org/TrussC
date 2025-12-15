#pragma once

// =============================================================================
// TrussC Serial Communication
// POSIX API ベースのシリアル通信クラス
// =============================================================================

#include <string>
#include <vector>
#include <cstdint>

// POSIX ヘッダー
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cstring>

namespace trussc {

// ---------------------------------------------------------------------------
// シリアルデバイス情報
// ---------------------------------------------------------------------------

struct SerialDeviceInfo {
    int deviceId;           // デバイスインデックス
    std::string devicePath; // デバイスパス（例: /dev/tty.usbserial-A10172HG）
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
    Serial() : fd_(-1), initialized_(false) {}

    ~Serial() {
        close();
    }

    // コピー禁止
    Serial(const Serial&) = delete;
    Serial& operator=(const Serial&) = delete;

    // ムーブ可能
    Serial(Serial&& other) noexcept
        : fd_(other.fd_), initialized_(other.initialized_), devicePath_(std::move(other.devicePath_)) {
        other.fd_ = -1;
        other.initialized_ = false;
    }

    Serial& operator=(Serial&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            initialized_ = other.initialized_;
            devicePath_ = std::move(other.devicePath_);
            other.fd_ = -1;
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
        printf("Serial devices:\n");
        for (const auto& dev : devices) {
            printf("  [%d] %s\n", dev.deviceId, dev.devicePath.c_str());
        }
    }

    // 利用可能なシリアルデバイスの一覧を取得
    std::vector<SerialDeviceInfo> getDeviceList() {
        std::vector<SerialDeviceInfo> devices;

#if defined(__APPLE__)
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

        // デバイスを開く（非ブロッキング）
        fd_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ == -1) {
            printf("Serial: failed to open %s\n", portName.c_str());
            return false;
        }

        // 排他ロックを取得
        if (ioctl(fd_, TIOCEXCL) == -1) {
            printf("Serial: failed to get exclusive access\n");
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // 端末設定を取得
        struct termios options;
        if (tcgetattr(fd_, &options) == -1) {
            printf("Serial: failed to get terminal attributes\n");
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
            printf("Serial: failed to set terminal attributes\n");
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // バッファをフラッシュ
        tcflush(fd_, TCIOFLUSH);

        devicePath_ = portName;
        initialized_ = true;
        printf("Serial: connected to %s at %d baud\n", portName.c_str(), baudRate);
        return true;
    }

    // デバイスインデックスを指定して接続
    bool setup(int deviceIndex, int baudRate) {
        auto devices = getDeviceList();
        if (deviceIndex < 0 || deviceIndex >= (int)devices.size()) {
            printf("Serial: device index %d out of range (0-%d)\n",
                   deviceIndex, (int)devices.size() - 1);
            return false;
        }
        return setup(devices[deviceIndex].devicePath, baudRate);
    }

    // 切断
    void close() {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
            printf("Serial: disconnected from %s\n", devicePath_.c_str());
        }
        initialized_ = false;
    }

    // ---------------------------------------------------------------------------
    // 状態確認
    // ---------------------------------------------------------------------------

    // 接続中かどうか
    bool isInitialized() const {
        return initialized_ && fd_ != -1;
    }

    // 接続中のデバイスパスを取得
    const std::string& getDevicePath() const {
        return devicePath_;
    }

    // 読み取り可能なバイト数を取得
    int available() const {
        if (!isInitialized()) return 0;

        int bytesAvailable = 0;
        if (ioctl(fd_, FIONREAD, &bytesAvailable) == -1) {
            return 0;
        }
        return bytesAvailable;
    }

    // ---------------------------------------------------------------------------
    // 読み取り
    // ---------------------------------------------------------------------------

    // 指定バイト数を読み取り
    // 戻り値: 実際に読み取ったバイト数（0以上）、エラー時は-1
    int readBytes(void* buffer, int length) {
        if (!isInitialized()) return -1;
        if (length <= 0) return 0;

        ssize_t result = read(fd_, buffer, length);
        if (result == -1) {
            // EAGAIN/EWOULDBLOCK は「データがない」状態なので0を返す
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        return static_cast<int>(result);
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
        ssize_t result = read(fd_, &byte, 1);
        if (result == 1) {
            return byte;
        } else if (result == 0 || (result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            return -1;  // データがない
        }
        return -2;  // エラー
    }

    // ---------------------------------------------------------------------------
    // 書き込み
    // ---------------------------------------------------------------------------

    // 指定バイト数を書き込み
    // 戻り値: 実際に書き込んだバイト数、エラー時は-1
    int writeBytes(const void* buffer, int length) {
        if (!isInitialized()) return -1;
        if (length <= 0) return 0;

        ssize_t result = write(fd_, buffer, length);
        if (result == -1) {
            return -1;
        }
        return static_cast<int>(result);
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
        if (isInitialized()) {
            tcflush(fd_, TCIFLUSH);
        }
    }

    // 出力バッファをフラッシュ
    void flushOutput() {
        if (isInitialized()) {
            tcflush(fd_, TCOFLUSH);
        }
    }

    // 入出力バッファをフラッシュ
    void flush() {
        if (isInitialized()) {
            tcflush(fd_, TCIOFLUSH);
        }
    }

    // 出力が完了するまで待機
    void drain() {
        if (isInitialized()) {
            tcdrain(fd_);
        }
    }

private:
    int fd_;               // ファイルディスクリプタ
    bool initialized_;     // 接続状態
    std::string devicePath_; // 接続中のデバイスパス

    // ボーレートを speed_t に変換
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
                printf("Serial: unsupported baud rate %d, using 9600\n", baudRate);
                return B9600;
        }
    }
};

} // namespace trussc

namespace tc = trussc;
