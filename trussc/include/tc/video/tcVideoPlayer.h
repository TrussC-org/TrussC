#pragma once

// =============================================================================
// tcVideoPlayer.h - Video playback
// =============================================================================
// Platform: macOS uses AVFoundation, Windows uses Media Foundation,
//           Linux uses FFmpeg
//
// Usage:
//   tc::VideoPlayer video;
//   video.load("movie.mp4");
//   video.play();
//
//   void update() {
//       video.update();
//   }
//
//   void draw() {
//       video.draw(0, 0);
//   }
// =============================================================================

#include "tcVideoPlayerBase.h"

namespace trussc {

// ---------------------------------------------------------------------------
// VideoPlayer - Standard video playback (RGBA output)
// ---------------------------------------------------------------------------
class VideoPlayer : public VideoPlayerBase {
public:
    VideoPlayer() = default;
    ~VideoPlayer() { close(); }

    // Move-enabled
    VideoPlayer(VideoPlayer&& other) noexcept {
        moveFrom(std::move(other));
    }

    VideoPlayer& operator=(VideoPlayer&& other) noexcept {
        if (this != &other) {
            close();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // =========================================================================
    // Load / Close
    // =========================================================================

    bool load(const std::string& path) override {
        if (initialized_) {
            close();
        }

        // Platform-specific load
        if (!loadPlatform(path)) {
            return false;
        }

        // Create texture (Stream mode: for per-frame updates)
        if (width_ > 0 && height_ > 0) {
            texture_.allocate(width_, height_, 4, TextureUsage::Stream);
            clearTexture();
        }

        initialized_ = true;
        firstFrameReceived_ = false;
        return true;
    }

    void close() override {
        if (!initialized_) return;

        closePlatform();

        texture_.clear();

        if (pixels_) {
            delete[] pixels_;
            pixels_ = nullptr;
        }

        initialized_ = false;
        playing_ = false;
        paused_ = false;
        frameNew_ = false;
        firstFrameReceived_ = false;
        done_ = false;
        width_ = 0;
        height_ = 0;
    }

    // =========================================================================
    // Update
    // =========================================================================

    void update() override {
        if (!initialized_) return;

        frameNew_ = false;

        // Platform-specific update
        updatePlatform();

        // Check for new frame from platform
        if (hasNewFramePlatform()) {
            // Update texture from pixel buffer
            std::lock_guard<std::mutex> lock(mutex_);
            if (pixels_ && width_ > 0 && height_ > 0) {
                texture_.loadData(pixels_, width_, height_, 4);
                markFrameNew();
            }
        }

        // Check if playback finished
        if (playing_ && !paused_ && isFinishedPlatform()) {
            markDone();
        }
    }

    // =========================================================================
    // Properties
    // =========================================================================

    float getDuration() const override {
        if (!initialized_) return 0.0f;
        return getDurationPlatform();
    }

    float getPosition() const override {
        if (!initialized_) return 0.0f;
        return getPositionPlatform();
    }

    // =========================================================================
    // Frame control
    // =========================================================================

    int getCurrentFrame() const override {
        if (!initialized_) return 0;
        return getCurrentFramePlatform();
    }

    int getTotalFrames() const override {
        if (!initialized_) return 0;
        return getTotalFramesPlatform();
    }

    void setFrame(int frame) override {
        if (!initialized_) return;
        setFramePlatform(frame);
    }

    void nextFrame() override {
        if (!initialized_) return;
        nextFramePlatform();
    }

    void previousFrame() override {
        if (!initialized_) return;
        previousFramePlatform();
    }

    // =========================================================================
    // Pixel access
    // =========================================================================

    unsigned char* getPixels() override { return pixels_; }
    const unsigned char* getPixels() const override { return pixels_; }

    // =========================================================================
    // Audio access
    // =========================================================================

    bool hasAudio() const override { return hasAudioPlatform(); }
    uint32_t getAudioCodec() const override { return getAudioCodecPlatform(); }
    std::vector<uint8_t> getAudioData() const override { return getAudioDataPlatform(); }
    int getAudioSampleRate() const override { return getAudioSampleRatePlatform(); }
    int getAudioChannels() const override { return getAudioChannelsPlatform(); }

protected:
    // -------------------------------------------------------------------------
    // Implementation methods
    // -------------------------------------------------------------------------

    void playImpl() override {
        playPlatform();
    }

    void stopImpl() override {
        stopPlatform();
        clearTexture();
    }

    void setPausedImpl(bool paused) override {
        setPausedPlatform(paused);
    }

    void setPositionImpl(float pct) override {
        setPositionPlatform(pct);
    }

    void setVolumeImpl(float vol) override {
        setVolumePlatform(vol);
    }

    void setSpeedImpl(float speed) override {
        setSpeedPlatform(speed);
    }

    void setPanImpl(float pan) override {
        if (pan != 0.0f) {
            logWarning("VideoPlayer") << "setPan not supported for native video (AVPlayer)";
        }
    }

    void setLoopImpl(bool loop) override {
        setLoopPlatform(loop);
    }

private:
    // Pixel data (RGBA)
    unsigned char* pixels_ = nullptr;

    // Platform-specific handle
    void* platformHandle_ = nullptr;

    // -------------------------------------------------------------------------
    // Internal methods
    // -------------------------------------------------------------------------

    void moveFrom(VideoPlayer&& other) {
        width_ = other.width_;
        height_ = other.height_;
        initialized_ = other.initialized_;
        playing_ = other.playing_;
        paused_ = other.paused_;
        frameNew_ = other.frameNew_;
        firstFrameReceived_ = other.firstFrameReceived_;
        done_ = other.done_;
        loop_ = other.loop_;
        volume_ = other.volume_;
        speed_ = other.speed_;
        pixels_ = other.pixels_;
        texture_ = std::move(other.texture_);
        platformHandle_ = other.platformHandle_;

        // Invalidate source
        other.pixels_ = nullptr;
        other.initialized_ = false;
        other.platformHandle_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }

    // Clear texture to black (prevents old frame from showing)
    void clearTexture() {
        if (width_ > 0 && height_ > 0 && pixels_) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::memset(pixels_, 0, width_ * height_ * 4);
            texture_.loadData(pixels_, width_, height_, 4);
        }
    }

    // -------------------------------------------------------------------------
    // Platform-specific methods (implemented in tcVideoPlayer_mac.mm, etc.)
    // -------------------------------------------------------------------------
    bool loadPlatform(const std::string& path);
    void closePlatform();
    void playPlatform();
    void stopPlatform();
    void setPausedPlatform(bool paused);
    void updatePlatform();

    bool hasNewFramePlatform() const;
    bool isFinishedPlatform() const;

    float getPositionPlatform() const;
    void setPositionPlatform(float pct);
    float getDurationPlatform() const;

    void setVolumePlatform(float vol);
    void setSpeedPlatform(float speed);
    void setLoopPlatform(bool loop);

    int getCurrentFramePlatform() const;
    int getTotalFramesPlatform() const;
    void setFramePlatform(int frame);
    void nextFramePlatform();
    void previousFramePlatform();

    // Audio access
    bool hasAudioPlatform() const;
    uint32_t getAudioCodecPlatform() const;
    std::vector<uint8_t> getAudioDataPlatform() const;
    int getAudioSampleRatePlatform() const;
    int getAudioChannelsPlatform() const;

    // Allow platform implementations to access internals
    friend class VideoPlayerPlatformAccess;
};

// Helper class for platform implementations to access protected members
class VideoPlayerPlatformAccess {
public:
    static void setDimensions(VideoPlayer& player, int w, int h) {
        player.width_ = w;
        player.height_ = h;
    }
    static void setPixelBuffer(VideoPlayer& player, unsigned char* pixels) {
        player.pixels_ = pixels;
    }
    static unsigned char*& getPixelBufferRef(VideoPlayer& player) {
        return player.pixels_;
    }
    static void*& getPlatformHandleRef(VideoPlayer& player) {
        return player.platformHandle_;
    }
    static std::mutex& getMutex(VideoPlayer& player) {
        return player.mutex_;
    }
};

} // namespace trussc
