#pragma once

// =============================================================================
// tcVideoPlayerBase.h - Base class for video players
// =============================================================================
// Common interface and state management for VideoPlayer and HapPlayer.

#include <string>
#include <atomic>
#include <mutex>
#include <filesystem>
#include "tc/gpu/tcHasTexture.h"
#include "tc/events/tcEvent.h"
#include "tc/utils/tcLoadResult.h"
#include "tc/utils/tcLog.h"

namespace trussc {

// Runtime playback failure. errorCode is backend-specific, or zero if unavailable.
struct VideoErrorEventArgs {
    std::string message;
    int64_t errorCode = 0;
};

namespace internal {
// Backend callbacks only enqueue; the owner consumes errors in update().
class VideoErrorQueue {
public:
    void report(const std::string& message, int64_t code = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.message.empty()) pending_ = {message, code};
    }
    VideoErrorEventArgs take() {
        std::lock_guard<std::mutex> lock(mutex_);
        VideoErrorEventArgs result = std::move(pending_);
        pending_ = {};
        return result;
    }
private:
    std::mutex mutex_;
    VideoErrorEventArgs pending_;
};
} // namespace internal

// ---------------------------------------------------------------------------
// VideoPlayerBase - Abstract base class for video playback
// ---------------------------------------------------------------------------
class VideoPlayerBase : public HasTexture {
public:
    VideoPlayerBase() = default;
    virtual ~VideoPlayerBase() = default;

    // Non-copyable
    VideoPlayerBase(const VideoPlayerBase&) = delete;
    VideoPlayerBase& operator=(const VideoPlayerBase&) = delete;

    // =========================================================================
    // Load / Close (must be implemented by derived class)
    // =========================================================================

    virtual LoadResult load(const fs::path& path) = 0;
    virtual void close() = 0;
    virtual bool isLoaded() const { return initialized_; }

    // =========================================================================
    // Playback control
    // =========================================================================

    virtual void play() {
        if (!initialized_) return;
        // NOTE: firstFrameReceived_ (isReady) is NOT reset here - the texture
        // still holds the last picture, so drawing does not show black.
        // It resets only when the texture actually goes empty/black:
        // load(), stop() (clears the texture) and close().
        errorStopped_ = false;
        done_ = false;
        playImpl();
        playing_ = true;
        paused_ = false;
        // Reapply speed (some platforms reset to 1.0 on play)
        if (speed_ != 1.0f) {
            setSpeedImpl(speed_);
        }
    }

    virtual void stop() {
        if (!initialized_) return;
        stopImpl();
        playing_ = false;
        paused_ = false;
        done_ = false;
        // firstFrameReceived_ (isReady) is kept: the texture still holds a
        // real picture (players poster frame 0 on stop, or keep the last
        // frame). It only resets on load()/close(), when the texture is
        // actually empty.
    }

    virtual void setPaused(bool paused) {
        if (!initialized_) return;
        setPausedImpl(paused);
        paused_ = paused;
        // Reapply speed when resuming (some platforms reset to 1.0)
        if (!paused && speed_ != 1.0f) {
            setSpeedImpl(speed_);
        }
    }

    void togglePause() {
        setPaused(!paused_);
    }

    virtual void update() = 0;

    // =========================================================================
    // State queries
    // =========================================================================

    // Call update() on the main thread to receive runtime errors.
    // onError listeners are not moved; they stay with their original object.
    Event<VideoErrorEventArgs> onError;
    bool hasError() const { return !errorMessage_.empty(); }
    const std::string& getErrorMessage() const { return errorMessage_; }

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }
    bool isFrameNew() const { return frameNew_ && firstFrameReceived_; }
    // True while the texture holds a real picture — i.e. drawing shows
    // actual video, not the cleared (black) texture. False only between
    // load() and the first picture (poster or decoded frame); play(),
    // stop(), seeking and pausing all keep a real picture on the texture.
    bool isReady() const { return firstFrameReceived_; }
    bool isDone() const { return done_; }

    // =========================================================================
    // Properties
    // =========================================================================

    float getWidth() const { return static_cast<float>(width_); }
    float getHeight() const { return static_cast<float>(height_); }

    virtual float getDuration() const = 0;

    virtual float getPosition() const = 0;

    virtual void setPosition(float pct) {
        if (!initialized_) return;
        pct = (pct < 0.0f) ? 0.0f : (pct > 1.0f) ? 1.0f : pct;
        setPositionImpl(pct);
    }

    float getCurrentTime() const {
        return getPosition() * getDuration();
    }

    void setCurrentTime(float seconds) {
        float duration = getDuration();
        if (duration > 0.0f) {
            setPosition(seconds / duration);
        }
    }

    void setVolume(float vol) {
        volume_ = (vol < 0.0f) ? 0.0f : (vol > 1.0f) ? 1.0f : vol;
        if (initialized_) {
            setVolumeImpl(volume_);
        }
    }

    float getVolume() const { return volume_; }

    void setSpeed(float speed) {
        speed_ = (speed < 0.0f) ? 0.0f : speed;  // Min 0 (no reverse for standard players)
        if (initialized_) {
            setSpeedImpl(speed_);
        }
    }

    float getSpeed() const { return speed_; }

    void setPan(float pan) {
        pan_ = (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan;
        if (initialized_) {
            setPanImpl(pan_);
        }
    }

    float getPan() const { return pan_; }

    void setLoop(bool loop) {
        loop_ = loop;
        if (initialized_) {
            setLoopImpl(loop_);
        }
    }

    bool isLoop() const { return loop_; }

    // =========================================================================
    // Frame control
    // =========================================================================

    virtual int getCurrentFrame() const = 0;
    virtual int getTotalFrames() const = 0;
    virtual void setFrame(int frame) = 0;
    virtual void nextFrame() = 0;
    virtual void previousFrame() = 0;

    void firstFrame() {
        setFrame(0);
    }

    // =========================================================================
    // Pixel access
    // =========================================================================

    // Get raw RGBA pixel data (for encoding workflows)
    // Returns nullptr if no frame has been decoded yet
    virtual unsigned char* getPixels() = 0;
    virtual const unsigned char* getPixels() const = 0;

    // =========================================================================
    // Audio access (for encoding workflows)
    // =========================================================================

    // Check if video has audio track
    virtual bool hasAudio() const { return false; }

    // Get audio codec as FourCC ('aac ', 'mp3 ', 'opus', etc.)
    // Returns 0 if no audio
    virtual uint32_t getAudioCodec() const { return 0; }

    // Get raw audio data (original codec, not decoded)
    // Returns empty vector if no audio
    virtual std::vector<uint8_t> getAudioData() const { return {}; }

    // Get audio properties
    virtual int getAudioSampleRate() const { return 0; }
    virtual int getAudioChannels() const { return 0; }

    // =========================================================================
    // Hardware acceleration info
    // =========================================================================

    /// Returns true if hardware-accelerated decoding is currently active.
    /// Concrete players that use HW decode should override this.
    virtual bool isUsingHwAccel() const { return false; }

    /// Returns the name of the active decode backend.
    /// Possible values on the standard VideoPlayer:
    ///   - macOS:   "videotoolbox", "software"
    ///   - Windows: "mediafoundation", "software"
    ///   - Linux:   "vaapi", "v4l2m2m", "drm", "cuda", "software"
    /// Returns "none" when no video is loaded or the player has no HW path.
    virtual std::string getHwAccelName() const { return "none"; }

    // =========================================================================
    // AV sync control
    // =========================================================================

    /// Set the maximum allowed video/audio drift (in seconds) before a hard
    /// re-sync (video seeks to the audio position). Set to 0 or negative to
    /// disable hard re-sync. Affects the Linux (FFmpeg) backend and tcxHap's
    /// HapPlayer, which slews wall-clock time toward playing audio and uses
    /// this threshold for hard re-sync. Slewing remains enabled when <= 0.
    /// Other platforms delegate sync to their native framework.
    /// Default: 0.5s
    virtual void setResyncThreshold(float seconds) { resyncThreshold_ = seconds; }

    /// Get the current resync threshold in seconds.
    virtual float getResyncThreshold() const { return resyncThreshold_; }

    // =========================================================================
    // HasTexture implementation
    // =========================================================================

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

protected:
    // Safe on decoder threads. No listeners or playback state are touched here.
    void reportPlaybackError(const std::string& message, int64_t code = 0) {
        playbackErrors_.report(message, code);
    }

    // Derived update() calls this on the main thread, outside backend locks.
    // Return immediately when true: a listener may close or reload this player.
    bool dispatchPlaybackError(const char* logModule = "VideoPlayer") {
        auto error = playbackErrors_.take();
        if (error.message.empty() || errorStopped_) return false;
        errorMessage_ = error.message;
        errorStopped_ = true;
        setPausedImpl(true); // stop without rewinding or replacing the last frame
        playing_ = false;
        paused_ = false;
        frameNew_ = false;
        done_ = false;
        logError(logModule) << error.message << " (code " << error.errorCode << ")";
        onError.notify(error);
        return true;
    }

    void clearPlaybackError() {
        playbackErrors_.take();
        errorMessage_.clear();
        errorStopped_ = false;
    }

    void movePlaybackErrorFrom(VideoPlayerBase& other) {
        errorMessage_ = std::move(other.errorMessage_);
        errorStopped_ = other.errorStopped_;
        auto pending = other.playbackErrors_.take();
        if (!pending.message.empty()) reportPlaybackError(pending.message, pending.errorCode);
        other.clearPlaybackError();
    }

    std::string errorMessage_;
    bool errorStopped_ = false;
    internal::VideoErrorQueue playbackErrors_;

    // -------------------------------------------------------------------------
    // State (accessible to derived classes)
    // -------------------------------------------------------------------------
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    bool playing_ = false;
    bool paused_ = false;
    bool frameNew_ = false;
    bool firstFrameReceived_ = false;
    bool done_ = false;
    bool loop_ = false;
    float volume_ = 1.0f;
    float speed_ = 1.0f;
    float pan_ = 0.0f;
    float resyncThreshold_ = 0.5f;

    // Thread synchronization
    mutable std::mutex mutex_;

    // Texture
    Texture texture_;

    // -------------------------------------------------------------------------
    // Implementation methods (to be overridden by derived classes)
    // -------------------------------------------------------------------------
    virtual void playImpl() = 0;
    virtual void stopImpl() = 0;
    virtual void setPausedImpl(bool paused) = 0;
    virtual void setPositionImpl(float pct) = 0;
    virtual void setVolumeImpl(float vol) = 0;
    virtual void setSpeedImpl(float speed) = 0;
    virtual void setPanImpl(float pan) = 0;
    virtual void setLoopImpl(bool loop) = 0;

    // Helper to mark frame as new
    void markFrameNew() {
        frameNew_ = true;
        firstFrameReceived_ = true;
    }

    // Helper to mark playback as done
    void markDone() {
        done_ = true;
        if (!loop_) {
            playing_ = false;
        }
    }
};

} // namespace trussc
