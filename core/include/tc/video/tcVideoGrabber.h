#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// tcVideoGrabber.h - Webcam input
// =============================================================================

// This file is included from TrussC.h
// Texture, HasTexture must be included first
//
// Device lifecycle (docs/DEVICE_LIFECYCLE.md): setup(), isConnected(), close(),
// onDisconnect. A camera the OS reports lost (unplugged) is noticed by the
// backend and reported from update(): the grabber closes itself, then fires
// onDisconnect once. close() and the destructor never fire it. The backends
// that report a loss: macOS / iOS (AVCaptureDeviceWasDisconnectedNotification),
// Windows (MF_SOURCE_READERF_ERROR, MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED),
// Linux (ENODEV from select / VIDIOC_DQBUF / VIDIOC_QBUF), web (the camera
// track ends). Android has no capture yet.

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
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventArgs.h"

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

// Which camera setup() opens (#415). With a remembered name, the first camera
// in the fresh list with that name; no match fails, never another camera.
// Without one, the camera at the setDeviceID() index (name is that entry's
// name, empty if the list does not show it).
struct GrabberDevicePick {
    bool ok = false;
    int index = 0;
    std::string name;
};

inline GrabberDevicePick pickGrabberDevice(const std::vector<VideoDeviceInfo>& devices,
                                           const std::string& rememberedName, int index) {
    GrabberDevicePick pick;
    pick.ok = true;
    pick.index = index;
    if (!rememberedName.empty()) {
        pick.ok = false;
        for (const auto& d : devices) {
            if (d.deviceName == rememberedName) {
                pick.ok = true;
                pick.index = d.deviceId;
                pick.name = d.deviceName;
                break;
            }
        }
        return pick;
    }
    for (const auto& d : devices) {
        if (d.deviceId == index) { pick.name = d.deviceName; break; }
    }
    return pick;
}

// Frame FIFO shared between the capture thread and the main thread.
// Owned by the capture State; its address stays stable across handle moves.
struct GrabberFrameQueue {
    std::mutex mtx;
    std::deque<GrabberFrame> frames;
    // Bound (security): camera frames can outpace a stalled reader; an opt-in cap keeps queued frame memory finite. Overflow drops the oldest frame.
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

    // The OS reported the camera lost. Set once, by the backend, from any
    // thread (the capture thread, a notification); VideoGrabber::update()
    // reports it on the main thread. close() replaces the State, so a loss
    // recorded for a run the app already closed is never seen.
    std::atomic<bool> lost{false};
    std::string lostReason;  // guarded by mtx, valid once lost is true

    void reportLost(std::string reason) {
        if (stopped.load()) return;  // close() came first: nothing to report
        std::lock_guard<std::mutex> lock(mtx);
        if (lost.load()) return;
        lostReason = std::move(reason);
        lost.store(true);
    }

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

    // Specify camera to use by index (call before setup()). It also forgets
    // the camera name that setup() remembered, so the next setup() opens this
    // index.
    void setDeviceID(int deviceId) {
        deviceId_ = deviceId;
        reopenName_.clear();
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
    // Events
    // =========================================================================

    // The OS reported the camera lost (unplugged): fires once, from update()
    // on the main thread, after the grabber has closed itself, so
    // isConnected() is already false. e.reason says why. It does not fire
    // from close(), setup() or the destructor. A listener may call setup() to
    // reconnect, or the app polls isConnected(). Listeners stay with the
    // grabber they were added to: a move does not carry them over.
    Event<DeviceDisconnectEventArgs> onDisconnect;

    // =========================================================================
    // Setup / Close
    // =========================================================================

    // Start the camera at the requested size: the camera with the name
    // remembered from the last successful open, or, before any, the one chosen
    // by setDeviceID() (device 0 by default). An open camera is closed first,
    // without onDisconnect.
    // If permission is not granted, requests it and returns false.
    // Call update() every frame - it will automatically initialize once permission is granted.
    // The size is remembered for setup().
    bool setup(int width, int height) {
        close(); // A new State owns this run; queue capacity is preserved.

        lastWidth_ = width;
        lastHeight_ = height;
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

    // Start the camera again: the camera with the same name as the last one
    // that opened (found again in a fresh device list, because indices shift
    // when cameras come and go) and the size of the last setup(width, height).
    // Before any successful open it uses the setDeviceID() index (device 0 at
    // 640x480 by default). setDeviceID() forgets the remembered name. Two
    // cameras with the same name can't be told apart: the first one opens.
    // This is the reconnect call:
    //   if (!grabber.isConnected() && getElapsedTime() - lastTry > 2.0f) { lastTry = getElapsedTime(); grabber.setup(); }
    // Returns false and logs an error when the camera cannot be opened, also
    // when no camera has the remembered name (it never opens another one;
    // false without an error while the camera permission is pending).
    // Until the first isFrameNew() after setup, the texture can still show the
    // previous camera image (e.g. the last frame before an unplug).
    bool setup() {
        return setup(lastWidth_, lastHeight_);
    }

    // Stop camera. Never fires onDisconnect (that is for a camera the OS
    // reported lost).
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

        // The OS reported the camera lost: close, then tell the app
        if (state_->lost.load()) {
            handleLost();
            return;
        }

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

    // True only while the camera is open: after a successful setup() and
    // until close() or a loss. False while permission is pending.
    bool isConnected() const { return initialized_ && !state_->lost.load(); }
    // Older name of isConnected()
    bool isInitialized() const { return isConnected(); }
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
    // Camera frames can outpace a stalled reader; an opt-in cap keeps queued frame memory finite. Overflow drops the oldest frame.
    TC_PLATFORMS("macos,windows,linux") void setFrameQueueSize(size_t maxFrames) {
        state_->frameQueue.maxFrames.store(maxFrames, std::memory_order_relaxed);
        if (maxFrames == 0) {
            std::lock_guard<std::mutex> lock(state_->frameQueue.mtx);
            state_->frameQueue.frames.clear();
        }
    }

    // Camera frames can outpace a stalled reader; an opt-in cap keeps queued frame memory finite. Overflow drops the oldest frame.
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
    int lastWidth_ = 640;        // size of the last setup(), for setup()
    int lastHeight_ = 480;

    // State
    bool initialized_ = false;
    bool frameNew_ = false;
    bool verbose_ = false;
    bool pendingSetup_ = false;  // Waiting for permission
    std::string deviceName_;
    std::string reopenName_;     // listDevices() name of the last opened camera; setDeviceID() clears it

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
        lastWidth_ = other.lastWidth_;
        lastHeight_ = other.lastHeight_;
        initialized_ = other.initialized_;
        frameNew_ = other.frameNew_;
        verbose_ = other.verbose_;
        pendingSetup_ = other.pendingSetup_;
        deviceName_ = std::move(other.deviceName_);
        reopenName_ = std::move(other.reopenName_);
        state_ = std::move(other.state_);
        other.state_ = std::make_shared<internal::GrabberSharedState>();
        texture_ = std::move(other.texture_);

        // Invalidate source object
        other.initialized_ = false;
        other.pendingSetup_ = false;
    }

    // The backend reported the camera lost (state_->lost). Close, then fire
    // onDisconnect with the grabber already closed, so a listener may call
    // setup(). Main thread only (update()).
    void handleLost() {
        DeviceDisconnectEventArgs args;
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            args.reason = state_->lostReason;
        }
        logWarning("VideoGrabber") << "lost connection to " << deviceName_ << " (" << args.reason << ")";
        close();
        internal::ExternalEventDelivery<DeviceDisconnectEventArgs>::notify(onDisconnect, args);
    }

    // Complete setup after permission is granted
    bool completeSetup() {
        // Resolve the camera in a fresh device list: the remembered name wins
        // over the index, which can point at another camera after a replug.
        auto pick = internal::pickGrabberDevice(listDevices(), reopenName_, deviceId_);
        if (!pick.ok) {
            logError("VideoGrabber") << "No camera named \"" << reopenName_ << "\" is present";
            return false;
        }
        deviceId_ = pick.index;

        // Platform-specific setup (sets state_->width, state_->height)
        if (!setupPlatform()) {
            return false;
        }
        reopenName_ = pick.name;

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
