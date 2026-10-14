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
#include "tc/utils/tcKeptThreads.h"
#include "tc/utils/tcMainThread.h"

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

struct VideoGrabberTestAccess;

// Frame FIFO shared between the capture thread and the main thread.
// Owned by the capture State; its address stays stable across handle moves.
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

// Everything used by a capture run belongs to this state. Platform callbacks
// retain it for the duration of their work; none borrow the public handle.
struct GrabberSharedState {
    std::mutex mtx;
    std::atomic<bool> dirty{false};
    std::atomic<bool> stopped{false};
    std::unique_ptr<unsigned char[]> pixels;
    GrabberFrameQueue frameQueue;
    std::shared_ptr<void> platformData;
    int width = 0;
    int height = 0;
    int requestedWidth = 640;
    int requestedHeight = 480;
    uint32_t webId = 0;
    std::thread captureThread;
    KeptThreads keptThreads;

    ~GrabberSharedState() { keptThreads.release(captureThread); }
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
        close(); // A new State owns this run; queue capacity is preserved.

        state_->requestedWidth = width;
        state_->requestedHeight = height;

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
        auto queueSize = getFrameQueueSize();
        pendingSetup_ = false;
        state_->stopped.store(true);
        closePlatform();
        state_->keptThreads.release(state_->captureThread);
        state_->keptThreads.joinOthers();
        if (texture_.isAllocated() && !isMainThread()) {
            // GPU destruction belongs to the main thread too. This cleanup
            // owns the texture and must run even though the handle has died.
            auto texture = std::make_shared<Texture>(std::move(texture_));
            runOnMainThread([texture] { texture->clear(); });
        } else {
            texture_.clear();
        }
        // The last callback releases the old run, including its pixel buffer.
        state_ = std::make_shared<internal::GrabberSharedState>();

        setFrameQueueSize(queueSize);
        initialized_ = false;
        frameNew_ = false;
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
            if (newW > 0 && newH > 0 && (newW != state_->width || newH != state_->height)) {
                // Resize buffers on main thread
                resizeBuffers(newW, newH);
            }
            clearResizeFlag();
        }

        // Update texture if buffer was updated
        if (state_->dirty.exchange(false)) {
            std::lock_guard<std::mutex> lock(state_->mtx);
            texture_.loadData(state_->pixels.get(), state_->width, state_->height, 4);
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
    int getWidth() const { return state_->width; }
    int getHeight() const { return state_->height; }

    // Get current device name
    const std::string& getDeviceName() const { return deviceName_; }

    // =========================================================================
    // Pixel access
    // =========================================================================

    unsigned char* getPixels() { return state_->pixels.get(); }
    const unsigned char* getPixels() const { return state_->pixels.get(); }

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
        state_->frameQueue.maxFrames.store(maxFrames, std::memory_order_relaxed);
        if (maxFrames == 0) {
            std::lock_guard<std::mutex> lock(state_->frameQueue.mtx);
            state_->frameQueue.frames.clear();
        }
    }

    TC_PLATFORMS("macos,windows,linux") size_t getFrameQueueSize() const {
        return state_->frameQueue.maxFrames.load(std::memory_order_relaxed);
    }

    // Drain all frames captured since the last call (appended to out, oldest
    // first). Returns the number of frames appended. Frames are moved out of
    // the internal queue, so each frame is delivered exactly once.
    TC_PLATFORMS("macos,windows,linux") size_t getQueuedFrames(std::vector<GrabberFrame>& out) {
        std::lock_guard<std::mutex> lock(state_->frameQueue.mtx);
        size_t n = state_->frameQueue.frames.size();
        for (auto& f : state_->frameQueue.frames) out.push_back(std::move(f));
        state_->frameQueue.frames.clear();
        return n;
    }

    // Copy to Image
    void copyToImage(Image& image) const {
        if (!initialized_ || !state_->pixels) return;

        image.allocate(state_->width, state_->height, 4);
        std::lock_guard<std::mutex> lock(state_->mtx);
        std::memcpy(image.getPixelsData(), state_->pixels.get(), state_->width * state_->height * 4);
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
    friend struct internal::VideoGrabberTestAccess;

    int deviceId_ = 0;
    int desiredFrameRate_ = -1;  // -1 = unspecified (camera default)

    // State
    bool initialized_ = false;
    bool frameNew_ = false;
    bool verbose_ = false;
    bool pendingSetup_ = false;  // Waiting for permission
    std::string deviceName_;

    std::shared_ptr<internal::GrabberSharedState> state_ =
        std::make_shared<internal::GrabberSharedState>();

    // Texture (Stream mode)
    Texture texture_;

    // -------------------------------------------------------------------------
    // Internal methods
    // -------------------------------------------------------------------------

    void moveFrom(VideoGrabber&& other) {
        deviceId_ = other.deviceId_;
        desiredFrameRate_ = other.desiredFrameRate_;
        initialized_ = other.initialized_;
        frameNew_ = other.frameNew_;
        verbose_ = other.verbose_;
        pendingSetup_ = other.pendingSetup_;
        deviceName_ = std::move(other.deviceName_);
        state_ = std::move(other.state_);
        other.state_ = std::make_shared<internal::GrabberSharedState>();
        texture_ = std::move(other.texture_);

        // Invalidate source object
        other.initialized_ = false;
        other.pendingSetup_ = false;
    }

    // Complete setup after permission is granted
    bool completeSetup() {
        // Platform-specific setup (sets state_->width, state_->height)
        if (!setupPlatform()) {
            return false;
        }

        // Allocate pixel buffer
        size_t bufferSize = state_->width * state_->height * 4;
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->pixels = std::make_unique<unsigned char[]>(bufferSize);
            std::memset(state_->pixels.get(), 0, bufferSize);
        }

        // Set pixel buffer pointer for delegate
        updateDelegatePixels();

        // Create texture (Stream mode: for per-frame updates)
        texture_.allocate(state_->width, state_->height, 4, TextureUsage::Stream);

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
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->pixels.reset(newPixels);
            state_->width = newWidth;
            state_->height = newHeight;

            // Publish the new buffer before capture can use the old pointer.
            updateDelegatePixels();
        }

        // Recreate texture
        texture_.allocate(state_->width, state_->height, 4, TextureUsage::Stream);
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
