#pragma once

// =============================================================================
// tcxHapPlayer.h - HAP Video Player
// =============================================================================
// VideoPlayerBase implementation for HAP/HAPQ codec playback.
// Uses GPU-friendly DXT/BC compressed textures for efficient playback.
//
// Usage:
//   tcx::hap::HapPlayer player;
//   player.load("content.mov");  // HAP encoded MOV
//   player.play();
//
//   void update() { player.update(); }
//   void draw() { player.draw(0, 0); }
// =============================================================================

#include <TrussC.h>
#include "tcxMovParser.h"
#include "tcxHapDecoder.h"
#include "ycocg.glsl.h"

namespace tcx::hap {

// Uniform structs for YCoCg shader
struct YCoCgVsParams {
    float screenSize[2];
    float _pad[2];
};


// ---------------------------------------------------------------------------
// HapPlayer - HAP codec video playback (BC compressed texture output)
// ---------------------------------------------------------------------------
class HapPlayer : public tc::VideoPlayerBase {
public:
    HapPlayer() = default;
    ~HapPlayer() { close(); }

    // Non-copyable, move-enabled
    HapPlayer(const HapPlayer&) = delete;
    HapPlayer& operator=(const HapPlayer&) = delete;

    HapPlayer(HapPlayer&& other) noexcept {
        moveFrom(std::move(other));
    }

    HapPlayer& operator=(HapPlayer&& other) noexcept {
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

        // Parse MOV file
        if (!movParser_.open(path)) {
            tc::logError("HapPlayer") << "Failed to open: " << path;
            return false;
        }

        const auto& info = movParser_.getInfo();
        const auto* videoTrack = info.getVideoTrack();

        if (!videoTrack) {
            tc::logError("HapPlayer") << "No video track found";
            movParser_.close();
            return false;
        }

        if (!videoTrack->isHap()) {
            tc::logError("HapPlayer") << "Not a HAP codec (FourCC: "
                << MovParser::fourccToString(videoTrack->codecFourCC) << ")";
            movParser_.close();
            return false;
        }

        // Store video info
        width_ = videoTrack->width;
        height_ = videoTrack->height;
        duration_ = videoTrack->getDurationSeconds();
        totalFrames_ = static_cast<int>(videoTrack->samples.size());
        videoTrack_ = videoTrack;

        // Determine HAP format from first frame
        if (!videoTrack->samples.empty()) {
            std::vector<uint8_t> firstFrame;
            if (movParser_.readSample(*videoTrack, 0, firstFrame)) {
                hapFormat_ = getHapFrameFormat(firstFrame.data(), firstFrame.size());
            }
        }

        if (hapFormat_ == HapFormat::Unknown) {
            tc::logError("HapPlayer") << "Could not determine HAP format";
            movParser_.close();
            return false;
        }

        // Allocate frame buffer for decoded DXT data
        size_t bufferSize = calculateTextureSize(width_, height_, hapFormat_);
        frameBuffer_.resize(bufferSize);

        // Create compressed texture
        if (!createCompressedTexture()) {
            tc::logError("HapPlayer") << "Failed to create compressed texture";
            movParser_.close();
            return false;
        }

        // Load audio track if available
        audioTrack_ = info.getAudioTrack();
        if (audioTrack_) {
            loadAudio();
        }

        tc::logNotice("HapPlayer") << "Loaded: " << width_ << "x" << height_
            << ", " << totalFrames_ << " frames, "
            << duration_ << "s, format: " << static_cast<int>(hapFormat_)
            << (hasAudio_ ? ", with audio" : ", no audio");

        initialized_ = true;
        currentFrame_ = 0;
        return true;
    }

    void close() override {
        if (!initialized_) return;

        // Stop audio
        if (hasAudio_) {
            audioPlayer_.stop();
        }

        movParser_.close();
        texture_.clear();
        frameBuffer_.clear();
        videoTrack_ = nullptr;
        audioTrack_ = nullptr;
        hasAudio_ = false;

        initialized_ = false;
        playing_ = false;
        paused_ = false;
        frameNew_ = false;
        firstFrameReceived_ = false;
        done_ = false;
        width_ = 0;
        height_ = 0;
        duration_ = 0;
        totalFrames_ = 0;
        currentFrame_ = 0;
        playbackTime_ = 0;
        hapFormat_ = HapFormat::Unknown;
    }

    // =========================================================================
    // Update
    // =========================================================================

    void update() override {
        if (!initialized_) return;

        frameNew_ = false;

        if (playing_ && !paused_) {
            // Advance playback time
            playbackTime_ += tc::getDeltaTime() * speed_;

            // Calculate target frame
            int targetFrame = static_cast<int>(playbackTime_ / duration_ * totalFrames_);

            // Handle loop / end
            if (targetFrame >= totalFrames_) {
                if (loop_) {
                    playbackTime_ = fmod(playbackTime_, duration_);
                    targetFrame = static_cast<int>(playbackTime_ / duration_ * totalFrames_);
                } else {
                    targetFrame = totalFrames_ - 1;
                    markDone();
                }
            }

            // Decode new frame if needed
            if (targetFrame != currentFrame_ || !firstFrameReceived_) {
                if (decodeFrame(targetFrame)) {
                    currentFrame_ = targetFrame;
                    updateTexture();
                    markFrameNew();
                }
            }
        }
    }

    // =========================================================================
    // Properties
    // =========================================================================

    float getDuration() const override {
        return duration_;
    }

    float getPosition() const override {
        if (duration_ <= 0) return 0;
        return playbackTime_ / duration_;
    }

    // =========================================================================
    // Frame control
    // =========================================================================

    int getCurrentFrame() const override {
        return currentFrame_;
    }

    int getTotalFrames() const override {
        return totalFrames_;
    }

    void setFrame(int frame) override {
        if (!initialized_) return;
        frame = std::max(0, std::min(frame, totalFrames_ - 1));
        if (decodeFrame(frame)) {
            currentFrame_ = frame;
            playbackTime_ = (duration_ * frame) / totalFrames_;
            updateTexture();
            markFrameNew();
        }
    }

    void nextFrame() override {
        setFrame(currentFrame_ + 1);
    }

    void previousFrame() override {
        setFrame(currentFrame_ - 1);
    }

    // =========================================================================
    // HAP-specific
    // =========================================================================

    HapFormat getHapFormat() const { return hapFormat_; }

    // Check if a file is HAP encoded (static utility)
    static bool isHapFile(const std::string& path) {
        return MovParser::isHapFile(path);
    }

    // =========================================================================
    // Draw (overridden for YCoCg shader support)
    // =========================================================================

    void draw(float x, float y) const override {
        draw(x, y, static_cast<float>(width_), static_cast<float>(height_));
    }

    void draw(float x, float y, float w, float h) const override {
        if (!initialized_ || !texture_.isAllocated()) return;

        if (hapFormat_ == HapFormat::YCoCgDXT5) {
            // Use YCoCg shader for HAP-Q (shader loaded lazily inside)
            drawWithYCoCgShader(x, y, w, h);
        } else {
            // Standard texture draw for HAP/HAP Alpha
            texture_.draw(x, y, w, h);
        }
    }

protected:
    // -------------------------------------------------------------------------
    // Implementation methods
    // -------------------------------------------------------------------------

    void playImpl() override {
        playbackTime_ = 0;
        currentFrame_ = -1;  // Force first frame decode
        if (hasAudio_) {
            audioPlayer_.play();
        }
    }

    void stopImpl() override {
        playbackTime_ = 0;
        currentFrame_ = 0;
        if (hasAudio_) {
            audioPlayer_.stop();
        }
    }

    void setPausedImpl(bool paused) override {
        if (hasAudio_) {
            if (paused) {
                audioPlayer_.pause();
            } else {
                audioPlayer_.resume();
            }
        }
    }

    void setPositionImpl(float pct) override {
        playbackTime_ = pct * duration_;
        int targetFrame = static_cast<int>(pct * totalFrames_);
        setFrame(targetFrame);
        // Note: Sound doesn't support seeking yet, so audio may be out of sync
        // For now, restart audio from beginning if position changes
        if (hasAudio_ && playing_) {
            audioPlayer_.stop();
            audioPlayer_.play();
        }
    }

    void setVolumeImpl(float vol) override {
        if (hasAudio_) {
            audioPlayer_.setVolume(vol);
        }
    }

    void setSpeedImpl(float speed) override {
        if (hasAudio_) {
            audioPlayer_.setSpeed(speed);
        }
    }

    void setLoopImpl(bool loop) override {
        if (hasAudio_) {
            audioPlayer_.setLoop(loop);
        }
    }

private:
    MovParser movParser_;
    HapDecoder hapDecoder_;
    const MovTrack* videoTrack_ = nullptr;
    const MovTrack* audioTrack_ = nullptr;

    HapFormat hapFormat_ = HapFormat::Unknown;
    std::vector<uint8_t> frameBuffer_;
    std::vector<uint8_t> sampleBuffer_;

    float duration_ = 0;
    int totalFrames_ = 0;
    int currentFrame_ = 0;
    double playbackTime_ = 0;

    // Audio playback
    tc::Sound audioPlayer_;
    bool hasAudio_ = false;

    // YCoCg shader for HAP-Q
    mutable tc::Shader ycocgShader_;

    // -------------------------------------------------------------------------
    // Internal methods
    // -------------------------------------------------------------------------

    void moveFrom(HapPlayer&& other) {
        // Move base class state
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
        texture_ = std::move(other.texture_);

        // Move HapPlayer-specific state
        movParser_ = std::move(other.movParser_);
        videoTrack_ = other.videoTrack_;
        audioTrack_ = other.audioTrack_;
        hapFormat_ = other.hapFormat_;
        frameBuffer_ = std::move(other.frameBuffer_);
        sampleBuffer_ = std::move(other.sampleBuffer_);
        duration_ = other.duration_;
        totalFrames_ = other.totalFrames_;
        currentFrame_ = other.currentFrame_;
        playbackTime_ = other.playbackTime_;
        audioPlayer_ = std::move(other.audioPlayer_);
        hasAudio_ = other.hasAudio_;

        // Invalidate source
        other.initialized_ = false;
        other.videoTrack_ = nullptr;
        other.audioTrack_ = nullptr;
        other.hasAudio_ = false;
        other.width_ = 0;
        other.height_ = 0;
    }

    bool loadAudio() {
        if (!audioTrack_) return false;

        // Check codec type
        uint32_t codec = audioTrack_->codecFourCC;

        if (audioTrack_->isPcm()) {
            // PCM audio - read all samples and convert to float
            return loadPcmAudio();
        }
        else if (audioTrack_->isMp3()) {
            // MP3 audio - extract and decode
            return loadMp3Audio();
        }
        else {
            tc::logWarning("HapPlayer") << "Unsupported audio codec: "
                << MovParser::fourccToString(codec);
            return false;
        }
    }

    bool loadPcmAudio() {
        if (!audioTrack_) return false;

        // Calculate total audio data size
        size_t totalSize = 0;
        for (const auto& sample : audioTrack_->samples) {
            totalSize += sample.size;
        }

        // Read all audio samples
        std::vector<uint8_t> audioData;
        audioData.reserve(totalSize);

        for (size_t i = 0; i < audioTrack_->samples.size(); i++) {
            std::vector<uint8_t> sampleData;
            if (movParser_.readSample(*audioTrack_, i, sampleData)) {
                audioData.insert(audioData.end(), sampleData.begin(), sampleData.end());
            }
        }

        if (audioData.empty()) {
            tc::logWarning("HapPlayer") << "Failed to read PCM audio data";
            return false;
        }

        // Create SoundBuffer from PCM data
        tc::SoundBuffer buffer;
        bool bigEndian = audioTrack_->isBigEndianPcm();
        int bitsPerSample = audioTrack_->isFloatPcm() ? 32 : audioTrack_->bitsPerSample;

        if (!buffer.loadPcmFromMemory(audioData.data(), audioData.size(),
                                       audioTrack_->channels, audioTrack_->sampleRate,
                                       bitsPerSample, bigEndian)) {
            tc::logWarning("HapPlayer") << "Failed to load PCM audio";
            return false;
        }

        audioPlayer_.loadFromBuffer(buffer);
        hasAudio_ = true;

        tc::logNotice("HapPlayer") << "Loaded PCM audio: "
            << audioTrack_->channels << " ch, "
            << audioTrack_->sampleRate << " Hz";

        return true;
    }

    bool loadMp3Audio() {
        if (!audioTrack_) return false;

        // Calculate total MP3 data size
        size_t totalSize = 0;
        for (const auto& sample : audioTrack_->samples) {
            totalSize += sample.size;
        }

        // Read all MP3 data
        std::vector<uint8_t> mp3Data;
        mp3Data.reserve(totalSize);

        for (size_t i = 0; i < audioTrack_->samples.size(); i++) {
            std::vector<uint8_t> sampleData;
            if (movParser_.readSample(*audioTrack_, i, sampleData)) {
                mp3Data.insert(mp3Data.end(), sampleData.begin(), sampleData.end());
            }
        }

        if (mp3Data.empty()) {
            tc::logWarning("HapPlayer") << "Failed to read MP3 audio data";
            return false;
        }

        // Decode MP3 from memory
        tc::SoundBuffer buffer;
        if (!buffer.loadMp3FromMemory(mp3Data.data(), mp3Data.size())) {
            tc::logWarning("HapPlayer") << "Failed to decode MP3 audio";
            return false;
        }

        audioPlayer_.loadFromBuffer(buffer);
        hasAudio_ = true;

        tc::logNotice("HapPlayer") << "Loaded MP3 audio: "
            << buffer.channels << " ch, "
            << buffer.sampleRate << " Hz";

        return true;
    }

    bool decodeFrame(int frameIndex) {
        if (!videoTrack_ || frameIndex < 0 ||
            frameIndex >= static_cast<int>(videoTrack_->samples.size())) {
            return false;
        }

        // Read sample data from MOV
        if (!movParser_.readSample(*videoTrack_, frameIndex, sampleBuffer_)) {
            return false;
        }

        // Decode HAP frame to DXT data
        HapFormat outFormat;
        if (!hapDecoder_.decodeToBuffer(
                sampleBuffer_.data(), sampleBuffer_.size(),
                width_, height_,
                frameBuffer_.data(), frameBuffer_.size(),
                outFormat)) {
            return false;
        }

        return true;
    }

    bool createCompressedTexture() {
        // Map HAP format to sokol pixel format
        switch (hapFormat_) {
            case HapFormat::DXT1:
                compressedFormat_ = SG_PIXELFORMAT_BC1_RGBA;
                break;
            case HapFormat::DXT5:
            case HapFormat::YCoCgDXT5:  // TODO: Needs shader for YCoCg conversion
                compressedFormat_ = SG_PIXELFORMAT_BC3_RGBA;
                break;
            case HapFormat::BC7:
                compressedFormat_ = SG_PIXELFORMAT_BC7_RGBA;
                break;
            case HapFormat::RGTC1:
                compressedFormat_ = SG_PIXELFORMAT_BC4_R;
                break;
            default:
                return false;
        }

        // Check if format is supported
        sg_pixelformat_info info = sg_query_pixelformat(compressedFormat_);
        if (!info.sample) {
            tc::logError("HapPlayer") << "Compressed texture format not supported on this GPU";
            return false;
        }

        return true;
    }

    void updateTexture() {
        // Use Texture's compressed texture support
        if (texture_.isAllocated()) {
            texture_.updateCompressed(frameBuffer_.data(), frameBuffer_.size());
        } else {
            texture_.allocateCompressed(width_, height_, compressedFormat_,
                                        frameBuffer_.data(), frameBuffer_.size());
        }
    }

    sg_pixel_format compressedFormat_ = SG_PIXELFORMAT_NONE;

    // -------------------------------------------------------------------------
    // YCoCg shader drawing
    // -------------------------------------------------------------------------

    void initYCoCgShader() const {
        if (!ycocgShader_.isLoaded()) {
            tc::logNotice("HapPlayer") << "Loading YCoCg shader...";
            if (!ycocgShader_.load(ycocg_shader_desc)) {
                tc::logError("HapPlayer") << "Failed to load YCoCg shader!";
            } else {
                tc::logNotice("HapPlayer") << "YCoCg shader loaded successfully";
            }
        }
    }

    void drawWithYCoCgShader(float x, float y, float w, float h) const {
        // Lazy init shader
        initYCoCgShader();
        if (!ycocgShader_.isLoaded()) {
            // Fallback to standard draw (will show wrong colors)
            static bool warned = false;
            if (!warned) {
                tc::logWarning("HapPlayer") << "YCoCg shader not loaded, using fallback";
                warned = true;
            }
            texture_.draw(x, y, w, h);
            return;
        }

        // Setup uniforms
        YCoCgVsParams vsParams = {};
        vsParams.screenSize[0] = static_cast<float>(tc::getWindowWidth());
        vsParams.screenSize[1] = static_cast<float>(tc::getWindowHeight());

        // Draw with shader
        tc::pushShader(ycocgShader_);

        // Bind texture and sampler
        ycocgShader_.setTexture(0, texture_.getView(), texture_.getSampler());

        // Set vertex shader uniforms
        ycocgShader_.setUniform(0, &vsParams, sizeof(vsParams));

        // Create textured quad vertices directly for shader
        tc::ShaderVertex verts[4];
        // Top-left
        verts[0] = {x, y, 0, 0.0f, 0.0f, 1, 1, 1, 1};
        // Top-right
        verts[1] = {x + w, y, 0, 1.0f, 0.0f, 1, 1, 1, 1};
        // Bottom-right
        verts[2] = {x + w, y + h, 0, 1.0f, 1.0f, 1, 1, 1, 1};
        // Bottom-left
        verts[3] = {x, y + h, 0, 0.0f, 1.0f, 1, 1, 1, 1};

        ycocgShader_.submitVertices(verts, 4, tc::PrimitiveType::Quads);

        tc::popShader();
    }
};

} // namespace tcx::hap
