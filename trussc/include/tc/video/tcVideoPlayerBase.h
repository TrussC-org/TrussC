#pragma once

// =============================================================================
// tcVideoPlayerBase.h - Base class for video players
// =============================================================================
// Common interface and state management for VideoPlayer and HapPlayer.

#include <string>
#include <atomic>
#include <mutex>
#include "tc/gpu/tcHasTexture.h"

namespace trussc {

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

    virtual bool load(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool isLoaded() const { return initialized_; }

    // =========================================================================
    // Playback control
    // =========================================================================

    virtual void play() {
        if (!initialized_) return;
        firstFrameReceived_ = false;
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
        firstFrameReceived_ = false;
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

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }
    bool isFrameNew() const { return frameNew_ && firstFrameReceived_; }
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
    // HasTexture implementation
    // =========================================================================

    Texture& getTexture() override { return texture_; }
    const Texture& getTexture() const override { return texture_; }

protected:
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
