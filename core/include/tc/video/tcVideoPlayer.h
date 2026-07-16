#pragma once
#include "tc/utils/tcAnnotations.h"

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

namespace internal { class VideoPlayerPlatformAccess; }  // friend, defined below

// ---------------------------------------------------------------------------
// VideoPlayer - Standard video playback (RGBA output)
// ---------------------------------------------------------------------------
class TC_PLATFORMS("macos,windows,linux,ios,web") VideoPlayer : public VideoPlayerBase {
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

    bool load(const fs::path& path) override {
        if (initialized_) {
            close();
        }

        // Resolve relative paths via getDataPath; URLs pass through untouched
        // (the web backend streams straight from them)
        const std::string pathStr = path.string();
        bool isUrl = pathStr.rfind("http://", 0) == 0 || pathStr.rfind("https://", 0) == 0;
        fs::path resolvedPath = isUrl ? path : getDataPath(path);

        // Platform-specific load
        if (!loadPlatform(resolvedPath)) {
            return false;
        }

        // Remember the resolved path so instance-level frame extraction
        // (extractFrame / extractKeyFrame with no path arg) and getPath() work.
        sourcePath_ = resolvedPath;

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
            }
        }

        initialized_ = true;
        firstFrameReceived_ = false;
        posterActive_ = false;

        // Auto poster: synchronously put frame 0 on the texture so drawing
        // never shows black between load() and the first decoded frame.
        // Frame 0 is always a keyframe, so this is exact AND fast.
        // Stream textures accept ONE loadData per frame, so the poster and
        // the black-clear are alternatives, never both.
        bool postered = autoPoster_ && loadPosterFrame(0.0f);
        if (!postered) clearTexture();
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
        posterActive_ = false;
        posterUploadPending_ = false;
        posterPx_.clear();
        texUploadFrame_ = ~0ull;
        lastShownTime_ = -1.0f;
        pendingSeekSec_ = -1.0f;
        done_ = false;
        width_ = 0;
        height_ = 0;
        sourcePath_.clear();
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
                    texUploadFrame_ = sapp_frame_count();
                    markFrameNew();
                }
            }
        }

        // A live frame replaces the poster; free the temporary poster
        // texture in NV12 mode (the shader path takes over). Track what
        // moment the picture on the texture belongs to (poster decisions).
        if (frameNew_) {
            lastShownTime_ = getCurrentTime();
            pendingSeekSec_ = -1.0f;  // live playback reflects the position now
            if (posterActive_) {
                posterActive_ = false;
                if (nv12Mode_) texture_.clear();
            }
        }

        // Check if playback finished
        if (playing_ && !paused_ && isFinishedPlatform()) {
            markDone();
        }

        // Deferred poster upload: the poster from stop()/seek+play() could not
        // use this texture's once-per-frame upload slot when it was requested
        // (a live frame had it). A live frame arriving now supersedes it;
        // otherwise the slot is free on this fresh frame - upload for real so
        // the picture on screen matches the playback position.
        if (posterUploadPending_) {
            if (frameNew_) {
                posterUploadPending_ = false;
                posterPx_.clear();
            } else if (posterPx_.isAllocated() && texture_.isAllocated()
                       && texUploadFrame_ != sapp_frame_count()) {
                texture_.loadData(posterPx_.getData(), width_, height_, 4);
                texUploadFrame_ = sapp_frame_count();
                posterUploadPending_ = false;
                posterPx_.clear();
            }
        }
    }

    // =========================================================================
    // Playback (auto poster on play)
    // =========================================================================

    void play() override {
        // Invariant: the texture always shows the frame AT the playback
        // position. If the position moved while stopped/paused (seek) or the
        // texture is still empty, bridge with the exact frame synchronously
        // so playback never starts on black or on a stale picture.
        if (autoPoster_ && initialized_) {
            float t = (pendingSeekSec_ >= 0.0f) ? pendingSeekSec_ : getCurrentTime();
            if (!firstFrameReceived_ || fabsf(t - lastShownTime_) > 0.05f) {
                loadPosterFrame(t);
            }
        }
        VideoPlayerBase::play();
    }

    // Auto poster (default ON): on load()/play(), synchronously extract the
    // frame at the current position and show it until live frames arrive, so
    // the player never draws its cleared (black) texture. Turn off if the
    // one-time synchronous decode at load/play is undesirable. No effect on
    // platforms without frame extraction (Android/web - web bridges via the
    // <video> element instead).
    void setAutoPoster(bool on) { autoPoster_ = on; }
    bool getAutoPoster() const { return autoPoster_; }

    // =========================================================================
    // Draw (NV12 path uses shader; RGBA path uses default HasTexture::draw)
    // =========================================================================

    void draw(float x, float y) const override {
        draw(x, y, (float)width_, (float)height_);
    }

    void draw(float x, float y, float w, float h) const override {
#if defined(__linux__) && !defined(__ANDROID__)
        // While the poster is up, draw it even in NV12 mode (the poster is an
        // RGBA texture; the Y/UV planes still hold priming data).
        if (nv12Mode_ && nv12ShaderHandle_ && !posterActive_) {
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
    TC_PLATFORMS("macos,windows,linux,ios") uint32_t getAudioCodec() const override { return getAudioCodecPlatform(); }
    TC_PLATFORMS("macos,windows,linux,ios") std::vector<uint8_t> getAudioData() const override { return getAudioDataPlatform(); }
    TC_PLATFORMS("macos,windows,linux,ios") int getAudioSampleRate() const override { return getAudioSampleRatePlatform(); }
    TC_PLATFORMS("macos,windows,linux,ios") int getAudioChannels() const override { return getAudioChannelsPlatform(); }

    // =========================================================================
    // Hardware acceleration info
    // =========================================================================

    bool isUsingHwAccel() const override { return isUsingHwAccelPlatform(); }
    std::string getHwAccelName() const override { return getHwAccelNamePlatform(); }

    /// Path of the currently loaded video file (empty string when not loaded).
    /// This is the resolved path (relative paths passed to load() are resolved
    /// via getDataPath), as UTF-8.
    fs::path getPath() const { return sourcePath_; }

protected:
    // -------------------------------------------------------------------------
    // Implementation methods
    // -------------------------------------------------------------------------

    void playImpl() override {
        playPlatform();
    }

    void stopImpl() override {
        stopPlatform();
        // stop() rewinds to the beginning, so put frame 0 on the texture:
        // the picture always matches the playback position, and a following
        // play() starts from an already-correct image (no black, no stale
        // frame). Platforms without extraction keep the last frame until the
        // element/decoder surfaces frame 0 itself.
        pendingSeekSec_ = -1.0f;
        if (autoPoster_) loadPosterFrame(0.0f);
    }

    void setPausedImpl(bool paused) override {
        setPausedPlatform(paused);
    }

    void setPositionImpl(float pct) override {
        setPositionPlatform(pct);
        // Seeks are async on most backends, so getCurrentTime() right after a
        // seek still returns the OLD position. Remember the target: the
        // poster logic in play() uses it until a live frame supersedes it.
        pendingSeekSec_ = pct * getDurationPlatform();
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
    bool  autoPoster_     = true;   // extract-and-show a poster on load/play
    bool  posterActive_   = false;  // poster currently on texture_ (until live)
    Pixels posterPx_;               // poster awaiting upload (deferred one frame)
    bool  posterUploadPending_ = false;  // upload posterPx_ on the next update()
    uint64_t texUploadFrame_ = ~0ull;    // sapp frame of the last texture_ upload
    float lastShownTime_  = -1.0f;  // time (sec) of the picture on the texture
    float pendingSeekSec_ = -1.0f;  // seek target awaiting playback (-1 = none)
    void* nv12ShaderHandle_ = nullptr;  // NV12VideoShader* on Linux/CUDA

    // Gamma correction (1.0 = none)
    float gammaCorrection_ = 1.0f;

    // HW decode preference (default on; Linux backend honors this)
    bool useHwAccel_ = true;

    // Platform-specific handle
    void* platformHandle_ = nullptr;

    // Resolved path of the currently loaded video (empty when not loaded).
    // Used by getPath() and the instance-level frame-extraction overloads.
    fs::path sourcePath_;

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
        autoPoster_      = other.autoPoster_;
        posterActive_    = other.posterActive_;
        posterPx_        = std::move(other.posterPx_);
        posterUploadPending_ = other.posterUploadPending_;
        texUploadFrame_  = other.texUploadFrame_;
        lastShownTime_   = other.lastShownTime_;
        pendingSeekSec_  = other.pendingSeekSec_;
        nv12ShaderHandle_ = other.nv12ShaderHandle_;
        platformHandle_  = other.platformHandle_;
        sourcePath_      = std::move(other.sourcePath_);

        other.pixels_    = nullptr;
        other.pixelsY_   = nullptr;
        other.pixelsUV_  = nullptr;
        other.posterUploadPending_ = false;
        other.texUploadFrame_      = ~0ull;
        other.nv12Mode_        = false;
        other.nv12ShaderHandle_ = nullptr;
        other.initialized_     = false;
        other.platformHandle_  = nullptr;
        other.width_  = 0;
        other.height_ = 0;
    }

    // Clear texture to black (prevents old frame from showing)
    // Extract the frame at timeSec and put it on texture_ as a poster.
    // timeSec <= 0 uses the keyframe path (frame 0 is a keyframe: exact+fast).
    // Marks the player ready (isReady) - drawing now shows a real picture.
    // Returns false when extraction is unavailable/failed (caller clears).
    bool loadPosterFrame(float timeSec) {
        if (sourcePath_.empty() || width_ <= 0 || height_ <= 0) return false;
        Pixels px;
        bool ok = (timeSec <= 0.001f)
                      ? extractKeyFramePlatform(sourcePath_, px, 0.0f, nullptr)
                      : extractFramePlatform(sourcePath_, px, timeSec, nullptr);
        if (!ok || px.getWidth() != width_ || px.getHeight() != height_) return false;
        bool freshTexture = !texture_.isAllocated();
        if (freshTexture) {
            texture_.allocate(width_, height_, 4, TextureUsage::Stream);
        }
        {
            // keep the CPU mirror in sync so getPixels() matches what is shown
            std::lock_guard<std::mutex> lock(mutex_);
            if (pixels_) {
                std::memcpy(pixels_, px.getData(), (size_t)width_ * height_ * 4);
            }
        }
        // texture_ accepts ONE loadData per app frame (sokol). During playback
        // the live frame usually took this frame's slot already, so uploading
        // now would be silently dropped and the screen would keep the stale
        // frame. Defer to the next update() in that case (the slot is free
        // there - or a new live frame supersedes the poster anyway).
        if (freshTexture || texUploadFrame_ != sapp_frame_count()) {
            texture_.loadData(px.getData(), width_, height_, 4);
            texUploadFrame_ = sapp_frame_count();
            posterUploadPending_ = false;
            posterPx_.clear();
        } else {
            posterPx_ = std::move(px);
            posterUploadPending_ = true;
        }
        posterActive_ = true;
        lastShownTime_ = timeSec;    // the picture now belongs to this moment
        firstFrameReceived_ = true;  // isReady: a real picture is on the texture
        return true;
    }

    void clearTexture() {
        if (width_ > 0 && height_ > 0 && pixels_) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::memset(pixels_, 0, width_ * height_ * 4);
            texture_.loadData(pixels_, width_, height_, 4);
            texUploadFrame_ = sapp_frame_count();
        }
    }

    // -------------------------------------------------------------------------
    // Platform-specific methods (implemented in tcVideoPlayer_mac.mm, etc.)
    // -------------------------------------------------------------------------
    bool loadPlatform(const fs::path& path);
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
    // Frame extraction (thread-safe, no GPU required)
    //
    // extractFrame    — the EXACT frame displayed at timeSec. Seeks to the
    //                   preceding keyframe then decodes forward to timeSec.
    //                   Slightly heavier, but frame-accurate on every platform.
    // extractKeyFrame — the NEAREST keyframe at or before timeSec. Seek only,
    //                   no forward decode, so it is faster but time-approximate.
    //                   Falls back to an exact decode if no keyframe is reachable.
    //
    // Both come in a static form (give a path, no load() needed — good for
    // thumbnails) and an instance form (uses the currently loaded getPath()).
    // These are single-shot helpers: do NOT use them for continuous playback
    // (each call opens its own decode context). For playback use load()/play().
    // =========================================================================
public:
    /// Extract the exact frame at a time from a video file (static).
    /// @param path      Video file path
    /// @param outPixels Receives the extracted frame (RGBA U8)
    /// @param timeSec   Time in seconds to extract from
    /// @param outDuration If non-null, receives video duration in seconds
    /// @return true on success
    static bool extractFrame(const fs::path& path, Pixels& outPixels,
                             float timeSec, float* outDuration = nullptr) {
        return extractFramePlatform(path, outPixels, timeSec, outDuration);
    }

    /// Extract the nearest keyframe at or before a time (static, faster).
    /// The returned frame's real time may be earlier than timeSec.
    /// @param path      Video file path
    /// @param outPixels Receives the extracted frame (RGBA U8)
    /// @param timeSec   Upper-bound time in seconds
    /// @param outDuration If non-null, receives video duration in seconds
    /// @return true on success
    static bool extractKeyFrame(const fs::path& path, Pixels& outPixels,
                                float timeSec, float* outDuration = nullptr) {
        return extractKeyFramePlatform(path, outPixels, timeSec, outDuration);
    }

    /// Extract the exact frame at a time from the currently loaded video
    /// (instance). Returns false if no video is loaded.
    bool extractFrame(Pixels& outPixels, float timeSec,
                      float* outDuration = nullptr) const {
        if (sourcePath_.empty()) return false;
        return extractFramePlatform(sourcePath_, outPixels, timeSec, outDuration);
    }

    /// Extract the nearest keyframe at or before a time from the currently
    /// loaded video (instance, faster). Returns false if no video is loaded.
    bool extractKeyFrame(Pixels& outPixels, float timeSec,
                         float* outDuration = nullptr) const {
        if (sourcePath_.empty()) return false;
        return extractKeyFramePlatform(sourcePath_, outPixels, timeSec, outDuration);
    }

    // -------------------------------------------------------------------------
    // Image convenience overloads. Same extraction, but the result lands in a
    // ready-to-draw Image (allocate + texture upload included). GPU upload =>
    // MAIN THREAD ONLY; use the Pixels overloads for background work.
    // -------------------------------------------------------------------------

    /// Extract the exact frame at a time into a ready-to-draw Image (static).
    static bool extractFrame(const fs::path& path, Image& outImage,
                             float timeSec, float* outDuration = nullptr) {
        Pixels px;
        if (!extractFramePlatform(path, px, timeSec, outDuration)) return false;
        pixelsToImage(px, outImage);
        return true;
    }

    /// Extract the nearest keyframe at or before a time into a ready-to-draw
    /// Image (static, faster).
    static bool extractKeyFrame(const fs::path& path, Image& outImage,
                                float timeSec, float* outDuration = nullptr) {
        Pixels px;
        if (!extractKeyFramePlatform(path, px, timeSec, outDuration)) return false;
        pixelsToImage(px, outImage);
        return true;
    }

    /// Extract the exact frame from the currently loaded video into a
    /// ready-to-draw Image (instance). Returns false if no video is loaded.
    bool extractFrame(Image& outImage, float timeSec,
                      float* outDuration = nullptr) const {
        if (sourcePath_.empty()) return false;
        return extractFrame(sourcePath_, outImage, timeSec, outDuration);
    }

    /// Extract the nearest keyframe from the currently loaded video into a
    /// ready-to-draw Image (instance, faster). Returns false if no video is loaded.
    bool extractKeyFrame(Image& outImage, float timeSec,
                         float* outDuration = nullptr) const {
        if (sourcePath_.empty()) return false;
        return extractKeyFrame(sourcePath_, outImage, timeSec, outDuration);
    }

private:
    // Move extracted pixels into a drawable Image (allocate + upload).
    static void pixelsToImage(const Pixels& px, Image& img) {
        img.allocate(px.getWidth(), px.getHeight(), 4);
        std::memcpy(img.getPixelsData(), px.getData(),
                    (size_t)px.getWidth() * px.getHeight() * 4);
        img.setDirty();
        img.update();
    }

    static bool extractFramePlatform(const fs::path& path, Pixels& outPixels,
                                     float timeSec, float* outDuration);
    static bool extractKeyFramePlatform(const fs::path& path, Pixels& outPixels,
                                        float timeSec, float* outDuration);

    // Allow platform implementations to access internals
    friend class internal::VideoPlayerPlatformAccess;

#if defined(__linux__) && !defined(__ANDROID__)
    void drawNV12Platform(float x, float y, float w, float h) const;
#endif
};

namespace internal {
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
} // namespace internal

} // namespace trussc
