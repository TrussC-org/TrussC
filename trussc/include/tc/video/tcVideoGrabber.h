#pragma once

// =============================================================================
// tcVideoGrabber.h - Webカメラ入力
// =============================================================================

// このファイルは TrussC.h からインクルードされる
// Texture, HasTexture が先にインクルードされている必要がある

#include <vector>
#include <string>
#include <atomic>
#include <mutex>

namespace trussc {

// ---------------------------------------------------------------------------
// VideoDeviceInfo - カメラデバイス情報
// ---------------------------------------------------------------------------
struct VideoDeviceInfo {
    int deviceId = -1;
    std::string deviceName;
    std::string uniqueId;

    int getDeviceID() const { return deviceId; }
    const std::string& getDeviceName() const { return deviceName; }
    const std::string& getUniqueId() const { return uniqueId; }
};

// ---------------------------------------------------------------------------
// VideoGrabber - Webカメラ入力クラス（HasTexture を継承）
// ---------------------------------------------------------------------------
class VideoGrabber : public HasTexture {
public:
    VideoGrabber() = default;
    ~VideoGrabber() { close(); }

    // コピー禁止
    VideoGrabber(const VideoGrabber&) = delete;
    VideoGrabber& operator=(const VideoGrabber&) = delete;

    // ムーブ対応
    VideoGrabber(VideoGrabber&& other) noexcept {
        moveFrom(std::move(other));
    }

    VideoGrabber& operator=(VideoGrabber&& other) noexcept {
        if (this != &other) {
            close();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // =========================================================================
    // デバイス管理
    // =========================================================================

    // 利用可能なカメラ一覧を取得
    std::vector<VideoDeviceInfo> listDevices() {
        return listDevicesPlatform();
    }

    // 使用するカメラを指定（setup() の前に呼ぶ）
    void setDeviceID(int deviceId) {
        deviceId_ = deviceId;
    }

    int getDeviceID() const {
        return deviceId_;
    }

    // 希望フレームレートを指定（setup() の前に呼ぶ）
    void setDesiredFrameRate(int fps) {
        desiredFrameRate_ = fps;
    }

    int getDesiredFrameRate() const {
        return desiredFrameRate_;
    }

    // 詳細ログのON/OFF
    void setVerbose(bool verbose) {
        verbose_ = verbose;
    }

    bool isVerbose() const {
        return verbose_;
    }

    // =========================================================================
    // セットアップ / クローズ
    // =========================================================================

    // カメラを開始（デフォルト 640x480）
    bool setup(int width = 640, int height = 480) {
        if (initialized_) {
            close();
        }

        requestedWidth_ = width;
        requestedHeight_ = height;

        // プラットフォーム固有のセットアップ（width_, height_ が設定される）
        if (!setupPlatform()) {
            return false;
        }

        // ピクセルバッファを確保
        size_t bufferSize = width_ * height_ * 4;
        pixels_ = new unsigned char[bufferSize];
        std::memset(pixels_, 0, bufferSize);

        // デリゲートにピクセルバッファのポインタを設定
        updateDelegatePixels();

        // テクスチャを作成（Stream モード: 毎フレーム更新用）
        texture_.allocate(width_, height_, 4, TextureUsage::Stream);

        initialized_ = true;
        return true;
    }

    // カメラを停止
    void close() {
        if (!initialized_) return;

        closePlatform();

        texture_.clear();

        if (pixels_) {
            delete[] pixels_;
            pixels_ = nullptr;
        }

        initialized_ = false;
        frameNew_ = false;
        width_ = 0;
        height_ = 0;
    }

    // =========================================================================
    // フレーム更新
    // =========================================================================

    // 新しいフレームをチェック（毎フレーム呼ぶ）
    void update() {
        if (!initialized_) return;

        frameNew_ = false;

        // プラットフォーム固有の更新処理
        updatePlatform();

        // カメラからのサイズ変更通知をチェック
        if (checkResizeNeeded()) {
            int newW = 0, newH = 0;
            getNewSize(newW, newH);
            if (newW > 0 && newH > 0 && (newW != width_ || newH != height_)) {
                // メインスレッドでバッファをリサイズ
                resizeBuffers(newW, newH);
            }
            clearResizeFlag();
        }

        // バッファが更新されていたらテクスチャに反映
        if (pixelsDirty_.exchange(false)) {
            std::lock_guard<std::mutex> lock(mutex_);
            texture_.loadData(pixels_, width_, height_, 4);
            frameNew_ = true;
        }
    }

    // 新しいフレームが来たか
    bool isFrameNew() const {
        return frameNew_;
    }

    // =========================================================================
    // 状態取得
    // =========================================================================

    bool isInitialized() const { return initialized_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // 現在のデバイス名を取得
    const std::string& getDeviceName() const { return deviceName_; }

    // =========================================================================
    // ピクセルアクセス
    // =========================================================================

    unsigned char* getPixels() { return pixels_; }
    const unsigned char* getPixels() const { return pixels_; }

    // Image にコピー
    void copyToImage(Image& image) const {
        if (!initialized_ || !pixels_) return;

        image.allocate(width_, height_, 4);
        std::lock_guard<std::mutex> lock(mutex_);
        std::memcpy(image.getPixelsData(), pixels_, width_ * height_ * 4);
        image.setDirty();
        image.update();
    }

    // =========================================================================
    // HasTexture 実装
    // =========================================================================

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

    // draw() は HasTexture のデフォルト実装を使用

    // =========================================================================
    // パーミッション（macOS）
    // =========================================================================

    // カメラ権限の状態を確認
    static bool checkCameraPermission();

    // カメラ権限をリクエスト（非同期）
    static void requestCameraPermission();

private:
    // サイズ
    int width_ = 0;
    int height_ = 0;
    int requestedWidth_ = 640;
    int requestedHeight_ = 480;
    int deviceId_ = 0;
    int desiredFrameRate_ = -1;  // -1 = 指定なし（カメラのデフォルト）

    // 状態
    bool initialized_ = false;
    bool frameNew_ = false;
    bool verbose_ = false;
    std::string deviceName_;

    // ピクセルデータ（RGBA）
    unsigned char* pixels_ = nullptr;

    // スレッド同期
    mutable std::mutex mutex_;
    std::atomic<bool> pixelsDirty_{false};

    // テクスチャ（Stream モード）
    Texture texture_;

    // プラットフォーム固有ハンドル
    void* platformHandle_ = nullptr;

    // -------------------------------------------------------------------------
    // 内部メソッド
    // -------------------------------------------------------------------------

    void moveFrom(VideoGrabber&& other) {
        width_ = other.width_;
        height_ = other.height_;
        requestedWidth_ = other.requestedWidth_;
        requestedHeight_ = other.requestedHeight_;
        deviceId_ = other.deviceId_;
        desiredFrameRate_ = other.desiredFrameRate_;
        initialized_ = other.initialized_;
        frameNew_ = other.frameNew_;
        verbose_ = other.verbose_;
        deviceName_ = std::move(other.deviceName_);
        pixels_ = other.pixels_;
        pixelsDirty_.store(other.pixelsDirty_.load());
        texture_ = std::move(other.texture_);
        platformHandle_ = other.platformHandle_;

        // 元のオブジェクトを無効化
        other.pixels_ = nullptr;
        other.initialized_ = false;
        other.platformHandle_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }

    // -------------------------------------------------------------------------
    // リサイズ処理
    // -------------------------------------------------------------------------
    void resizeBuffers(int newWidth, int newHeight) {
        // 新しいピクセルバッファを確保
        size_t newBufferSize = newWidth * newHeight * 4;
        unsigned char* newPixels = new unsigned char[newBufferSize];
        std::memset(newPixels, 0, newBufferSize);

        // mutexでロックして古いバッファと入れ替え
        {
            std::lock_guard<std::mutex> lock(mutex_);
            delete[] pixels_;
            pixels_ = newPixels;
            width_ = newWidth;
            height_ = newHeight;
        }

        // デリゲートに新しいポインタを通知
        updateDelegatePixels();

        // テクスチャを再作成
        texture_.allocate(width_, height_, 4, TextureUsage::Stream);
    }

    // -------------------------------------------------------------------------
    // プラットフォーム固有メソッド（tcVideoGrabber_mac.mm で実装）
    // -------------------------------------------------------------------------
    bool setupPlatform();
    void closePlatform();
    void updatePlatform();
    void updateDelegatePixels();  // ピクセルバッファ確保後にデリゲートを更新
    std::vector<VideoDeviceInfo> listDevicesPlatform();

    // リサイズ通知用（プラットフォーム実装）
    bool checkResizeNeeded();
    void getNewSize(int& width, int& height);
    void clearResizeFlag();
};

} // namespace trussc
