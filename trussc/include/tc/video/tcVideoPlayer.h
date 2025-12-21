#pragma once

// =============================================================================
// tcVideoPlayer.h - Video playback
// =============================================================================
// Platform: macOS uses AVFoundation, Windows uses Media Foundation (future)
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

#include <string>
#include <atomic>
#include <mutex>

namespace trussc {

// ---------------------------------------------------------------------------
// VideoPlayer - Video playback class (inherits HasTexture)
// ---------------------------------------------------------------------------
class VideoPlayer : public HasTexture {
public:
    VideoPlayer() = default;
    ~VideoPlayer() { close(); }

    // Non-copyable
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

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

    // Load video file
    // Supports: mp4, mov, m4v, etc. (platform-dependent)
    bool load(const std::string& path) {
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

    // Check if video is loaded
    bool isLoaded() const { return initialized_; }

    // Close video and release resources
    void close() {
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
    // Playback control
    // =========================================================================

    // Start playback
    void play() {
        if (!initialized_) return;

        // Clear texture to avoid showing old frame
        clearTexture();
        firstFrameReceived_ = false;
        done_ = false;

        playPlatform();
        playing_ = true;
        paused_ = false;
    }

    // Stop playback and reset to beginning
    void stop() {
        if (!initialized_) return;

        stopPlatform();
        playing_ = false;
        paused_ = false;
        done_ = false;

        // Clear texture
        clearTexture();
        firstFrameReceived_ = false;
    }

    // Pause/resume playback
    void setPaused(bool paused) {
        if (!initialized_) return;

        setPausedPlatform(paused);
        paused_ = paused;
    }

    // Toggle pause state
    void togglePause() {
        setPaused(!paused_);
    }

    // Update video state (call every frame in update())
    void update() {
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
                frameNew_ = true;
                firstFrameReceived_ = true;
            }
        }

        // Check if playback finished
        if (playing_ && !paused_ && isFinishedPlatform()) {
            done_ = true;
            if (!loop_) {
                playing_ = false;
            }
        }
    }

    // =========================================================================
    // State queries
    // =========================================================================

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }
    bool isFrameNew() const { return frameNew_ && firstFrameReceived_; }
    bool isDone() const { return done_; }

    // =========================================================================
    // Properties
    // =========================================================================

    float getWidth() const { return static_cast<float>(width_); }
    float getHeight() const { return static_cast<float>(height_); }

    // Get duration in seconds
    float getDuration() const {
        if (!initialized_) return 0.0f;
        return getDurationPlatform();
    }

    // Get current position (0.0 - 1.0)
    float getPosition() const {
        if (!initialized_) return 0.0f;
        return getPositionPlatform();
    }

    // Set position (0.0 - 1.0)
    void setPosition(float pct) {
        if (!initialized_) return;
        pct = (pct < 0.0f) ? 0.0f : (pct > 1.0f) ? 1.0f : pct;
        setPositionPlatform(pct);
    }

    // Get current time in seconds
    float getCurrentTime() const {
        return getPosition() * getDuration();
    }

    // Set current time in seconds
    void setCurrentTime(float seconds) {
        float duration = getDuration();
        if (duration > 0.0f) {
            setPosition(seconds / duration);
        }
    }

    // Volume (0.0 - 1.0)
    void setVolume(float vol) {
        volume_ = (vol < 0.0f) ? 0.0f : (vol > 1.0f) ? 1.0f : vol;
        if (initialized_) {
            setVolumePlatform(volume_);
        }
    }

    float getVolume() const { return volume_; }

    // Playback speed (1.0 = normal)
    void setSpeed(float speed) {
        speed_ = (speed < 0.0f) ? 0.0f : (speed > 4.0f) ? 4.0f : speed;
        if (initialized_) {
            setSpeedPlatform(speed_);
        }
    }

    float getSpeed() const { return speed_; }

    // Loop setting
    void setLoop(bool loop) {
        loop_ = loop;
        if (initialized_) {
            setLoopPlatform(loop_);
        }
    }

    bool isLoop() const { return loop_; }

    // =========================================================================
    // Frame control
    // =========================================================================

    // Get current frame number (0-based)
    int getCurrentFrame() const {
        if (!initialized_) return 0;
        return getCurrentFramePlatform();
    }

    // Get total number of frames
    int getTotalFrames() const {
        if (!initialized_) return 0;
        return getTotalFramesPlatform();
    }

    // Set frame by number (0-based)
    void setFrame(int frame) {
        if (!initialized_) return;
        setFramePlatform(frame);
    }

    // Advance to next frame
    void nextFrame() {
        if (!initialized_) return;
        nextFramePlatform();
    }

    // Go back to previous frame
    void previousFrame() {
        if (!initialized_) return;
        previousFramePlatform();
    }

    // Go to first frame
    void firstFrame() {
        setFrame(0);
    }

    // =========================================================================
    // Pixel access
    // =========================================================================

    unsigned char* getPixels() { return pixels_; }
    const unsigned char* getPixels() const { return pixels_; }

    // =========================================================================
    // HasTexture implementation
    // =========================================================================

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

    // draw() uses HasTexture's default implementation

private:
    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    bool playing_ = false;
    bool paused_ = false;
    bool frameNew_ = false;
    bool firstFrameReceived_ = false;  // Prevents showing old frame on play()
    bool done_ = false;
    bool loop_ = false;
    float volume_ = 1.0f;
    float speed_ = 1.0f;

    // Pixel data (RGBA)
    unsigned char* pixels_ = nullptr;

    // Thread synchronization
    mutable std::mutex mutex_;

    // Texture
    Texture texture_;

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
};

} // namespace trussc
