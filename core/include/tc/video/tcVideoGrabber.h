#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// tcVideoGrabber.h - Webcam input
// =============================================================================

// This file is included from TrussC.h
// Texture, HasTexture must be included first

#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <deque>
#include <memory>
#include <cstdint>
#include <cstring>

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
// GrabberFrame - One captured frame with its capture-time timestamp
// ---------------------------------------------------------------------------
// Pixels and timestamp travel together so there is no race between
// "get the pixels" and "get the time" (the next frame can never slip
// in between). timestampUs is a monotonic clock (std::chrono::steady_clock)
// value in microseconds, stamped on the capture thread the moment the frame
// arrives from the driver - it stays correct even if the app's main loop
// stalls. It is NOT wall-clock time; use it for interval math only.
struct GrabberFrame {
    Pixels pixels;             // RGBA8 (carries its own width/height/channels)
    uint64_t timestampUs = 0;  // steady_clock, microseconds since epoch of that clock
};

namespace internal {

// Frame FIFO shared between the capture thread and the main thread.
// Held by unique_ptr in VideoGrabber so the address stays stable across moves
// (the capture callback keeps a raw pointer to it).
struct GrabberFrameQueue {
    std::mutex mtx;
    std::deque<GrabberFrame> frames;
    std::atomic<size_t> maxFrames{0};  // 0 = queueing disabled

    // Called from the capture thread. Drops the oldest frame when full so a
    // stalled consumer never blocks capture (timestamps stay truthful).
    void push(const unsigned char* rgba, int w, int h, uint64_t tUs) {
        size_t cap = maxFrames.load(std::memory_order_relaxed);
        if (cap == 0 || !rgba || w <= 0 || h <= 0) return;
        GrabberFrame f;
        f.timestampUs = tUs;
        f.pixels.allocate(w, h, 4);
        std::memcpy(f.pixels.getData(), rgba, (size_t)w * h * 4);
        std::lock_guard<std::mutex> lock(mtx);
        while (frames.size() >= cap) frames.pop_front();
        frames.push_back(std::move(f));
    }
};

} // namespace internal

// ---------------------------------------------------------------------------
// VideoGrabber - Webcam input class (inherits HasTexture)
// ---------------------------------------------------------------------------
class TC_PLATFORMS("macos,windows,linux,ios,web") VideoGrabber : public HasTexture {
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
    // If permission is not granted, requests it and returns false.
    // Call update() every frame - it will automatically initialize once permission is granted.
    bool setup(int width = 640, int height = 480) {
        if (initialized_) {
            close();
        }

        requestedWidth_ = width;
        requestedHeight_ = height;

        // Check permission (macOS 10.14+)
        if (!checkCameraPermission()) {
            // Request permission and wait for it in update()
            requestCameraPermission();
            pendingSetup_ = true;
            return false;
        }

        return completeSetup();
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
    // Also handles pending setup after permission is granted
    void update() {
        // Handle pending setup (permission was requested in setup())
        if (pendingSetup_ && !initialized_) {
            if (checkCameraPermission()) {
                pendingSetup_ = false;
                completeSetup();
            }
            return;
        }

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
    bool isPendingPermission() const { return pendingSetup_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Get current device name
    const std::string& getDeviceName() const { return deviceName_; }

    // =========================================================================
    // Pixel access
    // =========================================================================

    unsigned char* getPixels() { return pixels_; }
    const unsigned char* getPixels() const { return pixels_; }

    // =========================================================================
    // Buffered frames (opt-in, timestamped)
    // =========================================================================
    // getPixels()/isFrameNew() only ever see the latest frame: if the camera
    // runs faster than the app loop (e.g. 90 fps camera, 60 fps app), the
    // frames in between are lost. Enable the frame queue to receive EVERY
    // frame, each stamped on the capture thread with a monotonic
    // (steady_clock) timestamp that stays truthful even when the main loop
    // stalls.

    // Enable/resize the internal frame queue (0 = disable, the default).
    // When full, the oldest frame is dropped. Sizing hint: keep it at least
    // ceil(cameraFps / appFps) + a little headroom; 8-16 is plenty.
    TC_PLATFORMS("macos,windows,linux") void setFrameQueueSize(size_t maxFrames) {
        frameQueue_->maxFrames.store(maxFrames, std::memory_order_relaxed);
        if (maxFrames == 0) {
            std::lock_guard<std::mutex> lock(frameQueue_->mtx);
            frameQueue_->frames.clear();
        }
    }

    TC_PLATFORMS("macos,windows,linux") size_t getFrameQueueSize() const {
        return frameQueue_->maxFrames.load(std::memory_order_relaxed);
    }

    // Drain all frames captured since the last call (appended to out, oldest
    // first). Returns the number of frames appended. Frames are moved out of
    // the internal queue, so each frame is delivered exactly once.
    TC_PLATFORMS("macos,windows,linux") size_t getBufferFrames(std::vector<GrabberFrame>& out) {
        std::lock_guard<std::mutex> lock(frameQueue_->mtx);
        size_t n = frameQueue_->frames.size();
        for (auto& f : frameQueue_->frames) out.push_back(std::move(f));
        frameQueue_->frames.clear();
        return n;
    }

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
    bool pendingSetup_ = false;  // Waiting for permission
    std::string deviceName_;

    // Pixel data (RGBA)
    unsigned char* pixels_ = nullptr;

    // Thread synchronization
    mutable std::mutex mutex_;
    std::atomic<bool> pixelsDirty_{false};

    // Timestamped frame FIFO (see getBufferFrames). unique_ptr keeps the
    // address stable across moves - the capture callback holds a raw pointer.
    std::unique_ptr<internal::GrabberFrameQueue> frameQueue_ =
        std::make_unique<internal::GrabberFrameQueue>();

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
        pendingSetup_ = other.pendingSetup_;
        deviceName_ = std::move(other.deviceName_);
        pixels_ = other.pixels_;
        pixelsDirty_.store(other.pixelsDirty_.load());
        texture_ = std::move(other.texture_);
        platformHandle_ = other.platformHandle_;
        frameQueue_ = std::move(other.frameQueue_);
        other.frameQueue_ = std::make_unique<internal::GrabberFrameQueue>();

        // Invalidate source object
        other.pixels_ = nullptr;
        other.initialized_ = false;
        other.pendingSetup_ = false;
        other.platformHandle_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }

    // Complete setup after permission is granted
    bool completeSetup() {
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
