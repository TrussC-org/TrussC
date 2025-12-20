#pragma once

// =============================================================================
// tcVideoGrabber.h - Webcam input
// =============================================================================

// This file is included from TrussC.h
// Texture, HasTexture must be included first

#include <vector>
#include <string>
#include <atomic>
#include <mutex>

namespace trussc {

// ---------------------------------------------------------------------------
// VideoDeviceInfo - Camera device information
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
// VideoGrabber - Webcam input class (inherits HasTexture)
// ---------------------------------------------------------------------------
class VideoGrabber : public HasTexture {
public:
    VideoGrabber() = default;
    ~VideoGrabber() { close(); }

    // Non-copyable
    VideoGrabber(const VideoGrabber&) = delete;
    VideoGrabber& operator=(const VideoGrabber&) = delete;

    // Move-enabled
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
    // Device management
    // =========================================================================

    // Get list of available cameras
    std::vector<VideoDeviceInfo> listDevices() {
        return listDevicesPlatform();
    }

    // Specify camera to use (call before setup())
    void setDeviceID(int deviceId) {
        deviceId_ = deviceId;
    }

    int getDeviceID() const {
        return deviceId_;
    }

    // Specify desired frame rate (call before setup())
    void setDesiredFrameRate(int fps) {
        desiredFrameRate_ = fps;
    }

    int getDesiredFrameRate() const {
        return desiredFrameRate_;
    }

    // Enable/disable verbose logging
    void setVerbose(bool verbose) {
        verbose_ = verbose;
    }

    bool isVerbose() const {
        return verbose_;
    }

    // =========================================================================
    // Setup / Close
    // =========================================================================

    // Start camera (default 640x480)
    bool setup(int width = 640, int height = 480) {
        if (initialized_) {
            close();
        }

        requestedWidth_ = width;
        requestedHeight_ = height;

        // Platform-specific setup (sets width_, height_)
        if (!setupPlatform()) {
            return false;
        }

        // Allocate pixel buffer
        size_t bufferSize = width_ * height_ * 4;
        pixels_ = new unsigned char[bufferSize];
        std::memset(pixels_, 0, bufferSize);

        // Set pixel buffer pointer for delegate
        updateDelegatePixels();

        // Create texture (Stream mode: for per-frame updates)
        texture_.allocate(width_, height_, 4, TextureUsage::Stream);

        initialized_ = true;
        return true;
    }

    // Stop camera
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
    // Frame update
    // =========================================================================

    // Check for new frame (call every frame)
    void update() {
        if (!initialized_) return;

        frameNew_ = false;

        // Platform-specific update processing
        updatePlatform();

        // Check for size change notification from camera
        if (checkResizeNeeded()) {
            int newW = 0, newH = 0;
            getNewSize(newW, newH);
            if (newW > 0 && newH > 0 && (newW != width_ || newH != height_)) {
                // Resize buffers on main thread
                resizeBuffers(newW, newH);
            }
            clearResizeFlag();
        }

        // Update texture if buffer was updated
        if (pixelsDirty_.exchange(false)) {
            std::lock_guard<std::mutex> lock(mutex_);
            texture_.loadData(pixels_, width_, height_, 4);
            frameNew_ = true;
        }
    }

    // Whether a new frame arrived
    bool isFrameNew() const {
        return frameNew_;
    }

    // =========================================================================
    // Status getters
    // =========================================================================

    bool isInitialized() const { return initialized_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Get current device name
    const std::string& getDeviceName() const { return deviceName_; }

    // =========================================================================
    // Pixel access
    // =========================================================================

    unsigned char* getPixels() { return pixels_; }
    const unsigned char* getPixels() const { return pixels_; }

    // Copy to Image
    void copyToImage(Image& image) const {
        if (!initialized_ || !pixels_) return;

        image.allocate(width_, height_, 4);
        std::lock_guard<std::mutex> lock(mutex_);
        std::memcpy(image.getPixelsData(), pixels_, width_ * height_ * 4);
        image.setDirty();
        image.update();
    }

    // =========================================================================
    // HasTexture implementation
    // =========================================================================

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

    // draw() uses HasTexture's default implementation

    // =========================================================================
    // Permissions (macOS)
    // =========================================================================

    // Check camera permission status
    static bool checkCameraPermission();

    // Request camera permission (async)
    static void requestCameraPermission();

private:
    // Size
    int width_ = 0;
    int height_ = 0;
    int requestedWidth_ = 640;
    int requestedHeight_ = 480;
    int deviceId_ = 0;
    int desiredFrameRate_ = -1;  // -1 = unspecified (camera default)

    // State
    bool initialized_ = false;
    bool frameNew_ = false;
    bool verbose_ = false;
    std::string deviceName_;

    // Pixel data (RGBA)
    unsigned char* pixels_ = nullptr;

    // Thread synchronization
    mutable std::mutex mutex_;
    std::atomic<bool> pixelsDirty_{false};

    // Texture (Stream mode)
    Texture texture_;

    // Platform-specific handle
    void* platformHandle_ = nullptr;

    // -------------------------------------------------------------------------
    // Internal methods
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

        // Invalidate source object
        other.pixels_ = nullptr;
        other.initialized_ = false;
        other.platformHandle_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }

    // -------------------------------------------------------------------------
    // Resize processing
    // -------------------------------------------------------------------------
    void resizeBuffers(int newWidth, int newHeight) {
        // Allocate new pixel buffer
        size_t newBufferSize = newWidth * newHeight * 4;
        unsigned char* newPixels = new unsigned char[newBufferSize];
        std::memset(newPixels, 0, newBufferSize);

        // Lock mutex and swap old buffer
        {
            std::lock_guard<std::mutex> lock(mutex_);
            delete[] pixels_;
            pixels_ = newPixels;
            width_ = newWidth;
            height_ = newHeight;
        }

        // Notify delegate of new pointer
        updateDelegatePixels();

        // Recreate texture
        texture_.allocate(width_, height_, 4, TextureUsage::Stream);
    }

    // -------------------------------------------------------------------------
    // Platform-specific methods (implemented in tcVideoGrabber_mac.mm)
    // -------------------------------------------------------------------------
    bool setupPlatform();
    void closePlatform();
    void updatePlatform();
    void updateDelegatePixels();  // Update delegate after pixel buffer allocation
    std::vector<VideoDeviceInfo> listDevicesPlatform();

    // For resize notification (platform implementation)
    bool checkResizeNeeded();
    void getNewSize(int& width, int& height);
    void clearResizeFlag();
};

} // namespace trussc
