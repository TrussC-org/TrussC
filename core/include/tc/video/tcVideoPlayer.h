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
#include "tc/graphics/tcPixels.h"

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

        // Resolve relative paths via getDataPath
        std::string resolvedPath = getDataPath(path);

        // Platform-specific load
        if (!loadPlatform(resolvedPath)) {
            return false;
        }

        // Create texture(s) for per-frame updates
        if (width_ > 0 && height_ > 0) {
            if (nv12Mode_) {
                textureY_.allocate(width_,     height_,     1,                  TextureUsage::Stream);
                textureUV_.allocate(width_ / 2, height_ / 2, TextureFormat::RG8, TextureUsage::Stream);
                // Prime the textures with the neutral-chroma CPU buffers
                // (Y=0, UV=128) set up in loadPlatform(). Without this, the
                // GPU textures contain uninitialized data and the BT.601
                // shader renders a green flash on the first frame (Y=0,
                // U=0, V=0 → RGB (0, 135, 0)).
                if (pixelsY_ && pixelsUV_) {
                    textureY_.loadData(pixelsY_,  width_,     height_,     1);
                    textureUV_.loadData(pixelsUV_, width_ / 2, height_ / 2, 2);
                }
            } else {
                texture_.allocate(width_, height_, 4, TextureUsage::Stream);
                clearTexture();
            }
        }

        initialized_ = true;
        firstFrameReceived_ = false;
        return true;
    }

    void close() override {
        if (!initialized_) return;

        closePlatform();

        texture_.clear();
        textureY_.clear();
        textureUV_.clear();

        if (pixels_)  { delete[] pixels_;  pixels_  = nullptr; }
        if (pixelsY_) { delete[] pixelsY_; pixelsY_ = nullptr; }
        if (pixelsUV_){ delete[] pixelsUV_; pixelsUV_ = nullptr; }
        nv12Mode_ = false;
        nv12ShaderHandle_ = nullptr;  // deleted in closePlatform()

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
            std::lock_guard<std::mutex> lock(mutex_);
            if (nv12Mode_) {
                if (pixelsY_ && pixelsUV_ && width_ > 0 && height_ > 0) {
                    textureY_.loadData(pixelsY_,  width_,     height_,     1);
                    textureUV_.loadData(pixelsUV_, width_ / 2, height_ / 2, 2);
                    markFrameNew();
                }
            } else {
                if (pixels_ && width_ > 0 && height_ > 0) {
                    texture_.loadData(pixels_, width_, height_, 4);
                    markFrameNew();
                }
            }
        }

        // Check if playback finished
        if (playing_ && !paused_ && isFinishedPlatform()) {
            markDone();
        }
    }

    // =========================================================================
    // Draw (NV12 path uses shader; RGBA path uses default HasTexture::draw)
    // =========================================================================

    void draw(float x, float y) const override {
        draw(x, y, (float)width_, (float)height_);
    }

    void draw(float x, float y, float w, float h) const override {
#if defined(__linux__) && !defined(__ANDROID__)
        if (nv12Mode_ && nv12ShaderHandle_) {
            drawNV12Platform(x, y, w, h);
            return;
        }
#endif
        if (texture_.isAllocated()) texture_.draw(x, y, w, h);
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
    // Gamma Correction
    // =========================================================================

    /// Set gamma correction value (1.0 = no correction)
    /// Used to fix color issues on some platforms (e.g. macOS AVFoundation returning dark colors)
    /// Typical values: 0.45 (1.0/2.2) to brighten, 2.2 to darken
    void setGammaCorrection(float gamma) {
        gammaCorrection_ = gamma;
    }

    /// Get current gamma correction value
    float getGammaCorrection() const {
        return gammaCorrection_;
    }

    // =========================================================================
    // Hardware decode control
    // =========================================================================

    /// Force software decoding (disable hardware acceleration).
    /// Must be called **before** load(). Default: true (use HW if available).
    /// Currently only affects the Linux backend.
    void setUseHwAccel(bool enable) {
        useHwAccel_ = enable;
    }

    /// Get current HW accel preference (not the actual backend in use —
    /// use isUsingHwAccel() for that).
    bool getUseHwAccel() const {
        return useHwAccel_;
    }

    // =========================================================================
    // Pixel access
    // =========================================================================

    unsigned char* getPixels() override { return pixels_; }
    const unsigned char* getPixels() const override { return pixels_; }

    unsigned char* getPixelsY()  { return pixelsY_; }
    unsigned char* getPixelsUV() { return pixelsUV_; }

    // =========================================================================
    // Audio access
    // =========================================================================

    bool hasAudio() const override { return hasAudioPlatform(); }
    uint32_t getAudioCodec() const override { return getAudioCodecPlatform(); }
    std::vector<uint8_t> getAudioData() const override { return getAudioDataPlatform(); }
    int getAudioSampleRate() const override { return getAudioSampleRatePlatform(); }
    int getAudioChannels() const override { return getAudioChannelsPlatform(); }

    // =========================================================================
    // Hardware acceleration info
    // =========================================================================

    bool isUsingHwAccel() const override { return isUsingHwAccelPlatform(); }
    std::string getHwAccelName() const override { return getHwAccelNamePlatform(); }

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

    // NV12 (CUDA HW decode) planes — Linux only, non-null when nv12Mode_ is set
    unsigned char* pixelsY_  = nullptr;
    unsigned char* pixelsUV_ = nullptr;
    Texture textureY_;
    Texture textureUV_;
    bool  nv12Mode_       = false;
    void* nv12ShaderHandle_ = nullptr;  // NV12VideoShader* on Linux/CUDA

    // Gamma correction (1.0 = none)
    float gammaCorrection_ = 1.0f;

    // HW decode preference (default on; Linux backend honors this)
    bool useHwAccel_ = true;

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
        pixels_    = other.pixels_;
        pixelsY_   = other.pixelsY_;
        pixelsUV_  = other.pixelsUV_;
        texture_   = std::move(other.texture_);
        textureY_  = std::move(other.textureY_);
        textureUV_ = std::move(other.textureUV_);
        nv12Mode_        = other.nv12Mode_;
        nv12ShaderHandle_ = other.nv12ShaderHandle_;
        platformHandle_  = other.platformHandle_;

        other.pixels_    = nullptr;
        other.pixelsY_   = nullptr;
        other.pixelsUV_  = nullptr;
        other.nv12Mode_        = false;
        other.nv12ShaderHandle_ = nullptr;
        other.initialized_     = false;
        other.platformHandle_  = nullptr;
        other.width_  = 0;
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

    // Hardware acceleration info
    bool isUsingHwAccelPlatform() const;
    std::string getHwAccelNamePlatform() const;

    // =========================================================================
    // Static utility — frame extraction (thread-safe, no GPU required)
    // =========================================================================
public:
    /// Extract a single frame as RGBA pixels from a video file.
    /// @param path      Video file path
    /// @param outPixels Receives the extracted frame (RGBA U8)
    /// @param timeSec   Time in seconds to extract from (default 1.0)
    /// @param outDuration If non-null, receives video duration in seconds
    /// @return true on success
    static bool extractFrame(const std::string& path, Pixels& outPixels,
                             float timeSec = 1.0f, float* outDuration = nullptr) {
        return extractFramePlatform(path, outPixels, timeSec, outDuration);
    }

private:
    static bool extractFramePlatform(const std::string& path, Pixels& outPixels,
                                     float timeSec, float* outDuration);

    // Allow platform implementations to access internals
    friend class VideoPlayerPlatformAccess;

#if defined(__linux__) && !defined(__ANDROID__)
    void drawNV12Platform(float x, float y, float w, float h) const;
#endif
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
