#pragma once

// =============================================================================
// tcTcvPlayer.h - TCVC video player
// =============================================================================

#include <TrussC.h>
#include <fstream>
#include <vector>
#include <cstring>

#include "tcTcvEncoder.h"  // For TcvHeader and constants

namespace tcx {

// ---------------------------------------------------------------------------
// TcvPlayer - Plays TCVC encoded video
// ---------------------------------------------------------------------------
class TcvPlayer : public tc::VideoPlayerBase {
public:
    TcvPlayer() = default;
    ~TcvPlayer() { close(); }

    // Non-copyable
    TcvPlayer(const TcvPlayer&) = delete;
    TcvPlayer& operator=(const TcvPlayer&) = delete;

    // =========================================================================
    // Load / Close
    // =========================================================================

    bool load(const std::string& path) override {
        if (initialized_) {
            close();
        }

        file_.open(path, std::ios::binary);
        if (!file_.is_open()) {
            tc::logError("TcvPlayer") << "Failed to open file: " << path;
            return false;
        }

        // Read header
        file_.read(reinterpret_cast<char*>(&header_), sizeof(header_));

        // Validate signature
        if (std::memcmp(header_.signature, "TCVC", 4) != 0) {
            tc::logError("TcvPlayer") << "Invalid TCVC signature";
            file_.close();
            return false;
        }

        // Validate version
        if (header_.version != TCV_VERSION) {
            tc::logError("TcvPlayer") << "Unsupported version: " << header_.version;
            file_.close();
            return false;
        }

        width_ = header_.width;
        height_ = header_.height;

        // Calculate block counts
        blocksX_ = (width_ + TCV_BLOCK_SIZE - 1) / TCV_BLOCK_SIZE;
        blocksY_ = (height_ + TCV_BLOCK_SIZE - 1) / TCV_BLOCK_SIZE;

        // Calculate expected BC7 data size per frame
        // Each 16x16 block = 16 BC7 4x4 blocks = 16 * 16 bytes = 256 bytes
        frameDataSize_ = blocksX_ * blocksY_ * 16 * 16;

        // Allocate BC7 buffer
        bc7Buffer_.resize(frameDataSize_);

        // Build frame index for seeking
        buildFrameIndex();

        // Allocate texture as BC7 compressed
        int paddedWidth = blocksX_ * TCV_BLOCK_SIZE;
        int paddedHeight = blocksY_ * TCV_BLOCK_SIZE;
        texture_.allocateCompressed(paddedWidth, paddedHeight,
                                    SG_PIXELFORMAT_BC7_RGBA,
                                    nullptr, 0);

        initialized_ = true;
        currentFrame_ = -1;

        tc::logNotice("TcvPlayer") << "Loaded: " << width_ << "x" << height_
                                   << " @ " << header_.fps << " fps, "
                                   << header_.frameCount << " frames";
        return true;
    }

    void close() override {
        if (!initialized_) return;

        file_.close();
        texture_.clear();
        frameOffsets_.clear();
        bc7Buffer_.clear();

        initialized_ = false;
        playing_ = false;
        paused_ = false;
        frameNew_ = false;
        firstFrameReceived_ = false;
        done_ = false;
        currentFrame_ = -1;
    }

    // =========================================================================
    // Update
    // =========================================================================

    void update() override {
        if (!initialized_ || !playing_ || paused_) return;

        frameNew_ = false;

        // Calculate target frame based on elapsed time
        float elapsed = tc::getElapsedTimef() - playStartTime_;
        int targetFrame = static_cast<int>(elapsed * header_.fps);

        if (targetFrame >= static_cast<int>(header_.frameCount)) {
            if (loop_) {
                playStartTime_ = tc::getElapsedTimef();
                targetFrame = 0;
            } else {
                markDone();
                return;
            }
        }

        if (targetFrame != currentFrame_) {
            decodeFrame(targetFrame);
            currentFrame_ = targetFrame;
            markFrameNew();
        }
    }

    // =========================================================================
    // Properties
    // =========================================================================

    float getDuration() const override {
        if (!initialized_ || header_.fps <= 0) return 0.0f;
        return static_cast<float>(header_.frameCount) / header_.fps;
    }

    float getPosition() const override {
        if (!initialized_ || header_.frameCount == 0) return 0.0f;
        return static_cast<float>(currentFrame_) / header_.frameCount;
    }

    // =========================================================================
    // Frame control
    // =========================================================================

    int getCurrentFrame() const override {
        return currentFrame_ >= 0 ? currentFrame_ : 0;
    }

    int getTotalFrames() const override {
        return initialized_ ? header_.frameCount : 0;
    }

    void setFrame(int frame) override {
        if (!initialized_) return;
        frame = std::max(0, std::min(frame, static_cast<int>(header_.frameCount) - 1));
        if (frame != currentFrame_) {
            decodeFrame(frame);
            currentFrame_ = frame;
            markFrameNew();
        }
    }

    void nextFrame() override {
        setFrame(currentFrame_ + 1);
    }

    void previousFrame() override {
        setFrame(currentFrame_ - 1);
    }

protected:
    void playImpl() override {
        playStartTime_ = tc::getElapsedTimef();
        currentFrame_ = -1;
    }

    void stopImpl() override {
        currentFrame_ = -1;
    }

    void setPausedImpl(bool paused) override {
        // TODO: Handle pause timer properly
    }

    void setPositionImpl(float pct) override {
        int frame = static_cast<int>(pct * header_.frameCount);
        setFrame(frame);
        // Adjust start time so elapsed time matches the new position
        playStartTime_ = tc::getElapsedTimef() - pct * getDuration();
    }

    void setVolumeImpl(float vol) override {
        // TODO: Audio support in Phase 4
    }

    void setSpeedImpl(float speed) override {
        // TODO: Playback speed support
    }

    void setLoopImpl(bool loop) override {
        // Already handled by base class
    }

private:
    std::ifstream file_;
    TcvHeader header_ = {};

    int blocksX_ = 0;
    int blocksY_ = 0;
    size_t frameDataSize_ = 0;

    std::vector<uint8_t> bc7Buffer_;
    std::vector<std::streampos> frameOffsets_;
    int currentFrame_ = -1;

    float playStartTime_ = 0.0f;  // Time when playback started

    // Build index of frame offsets for seeking
    void buildFrameIndex() {
        frameOffsets_.clear();
        frameOffsets_.reserve(header_.frameCount);

        file_.seekg(header_.headerSize);

        for (uint32_t i = 0; i < header_.frameCount; i++) {
            frameOffsets_.push_back(file_.tellg());

            uint8_t packetType;
            uint32_t dataSize;
            file_.read(reinterpret_cast<char*>(&packetType), 1);
            file_.read(reinterpret_cast<char*>(&dataSize), 4);
            file_.seekg(dataSize, std::ios::cur);
        }
    }

    // Decode a specific frame
    void decodeFrame(int frameIndex) {
        if (frameIndex < 0 || frameIndex >= static_cast<int>(frameOffsets_.size())) {
            return;
        }

        file_.seekg(frameOffsets_[frameIndex]);

        uint8_t packetType;
        uint32_t dataSize;
        file_.read(reinterpret_cast<char*>(&packetType), 1);
        file_.read(reinterpret_cast<char*>(&dataSize), 4);

        if (packetType == TCV_PACKET_NEW_FRAME) {
            // Read BC7 data
            file_.read(reinterpret_cast<char*>(bc7Buffer_.data()), dataSize);

            // Upload to GPU texture
            texture_.updateCompressed(bc7Buffer_.data(), dataSize);
        }
        // TODO: Handle TCV_PACKET_REF_FRAME in Phase 3
    }
};

} // namespace tcx
