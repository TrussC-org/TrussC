#pragma once

// =============================================================================
// tcTcvPlayer.h - TCVC video player (v2: I/P/REF frame support)
// =============================================================================

#include <TrussC.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <unordered_map>

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
            tc::logError("TcvPlayer") << "Unsupported version: " << header_.version
                                      << " (expected " << TCV_VERSION << ")";
            file_.close();
            return false;
        }

        width_ = header_.width;
        height_ = header_.height;

        // Calculate block counts
        blocksX_ = (width_ + TCV_BLOCK_SIZE - 1) / TCV_BLOCK_SIZE;
        blocksY_ = (height_ + TCV_BLOCK_SIZE - 1) / TCV_BLOCK_SIZE;
        totalBlocks_ = blocksX_ * blocksY_;

        // Calculate BC7 data size per frame
        bc7FrameSize_ = totalBlocks_ * 256;  // 16 BC7 blocks per 16x16, 16 bytes each

        // Allocate BC7 buffer
        bc7Buffer_.resize(bc7FrameSize_);

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
        frameIndex_.clear();
        iFrameCache_.clear();
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
    int totalBlocks_ = 0;
    size_t bc7FrameSize_ = 0;

    std::vector<uint8_t> bc7Buffer_;
    int currentFrame_ = -1;

    float playStartTime_ = 0.0f;

    // Frame index entry
    struct FrameIndexEntry {
        std::streampos offset;
        uint8_t packetType;
        uint32_t refFrame;      // For P/REF frames
        uint32_t dataSize;
    };
    std::vector<FrameIndexEntry> frameIndex_;

    // I-frame cache: frameNumber -> BC7 data
    std::unordered_map<int, std::vector<uint8_t>> iFrameCache_;

    // Build index of frame offsets for seeking
    void buildFrameIndex() {
        frameIndex_.clear();
        frameIndex_.reserve(header_.frameCount);
        iFrameCache_.clear();

        file_.seekg(header_.headerSize);

        for (uint32_t i = 0; i < header_.frameCount; i++) {
            FrameIndexEntry entry;
            entry.offset = file_.tellg();

            file_.read(reinterpret_cast<char*>(&entry.packetType), 1);

            if (entry.packetType == TCV_PACKET_I_FRAME) {
                file_.read(reinterpret_cast<char*>(&entry.dataSize), 4);
                entry.refFrame = 0;
                file_.seekg(entry.dataSize, std::ios::cur);
            } else if (entry.packetType == TCV_PACKET_P_FRAME) {
                file_.read(reinterpret_cast<char*>(&entry.refFrame), 4);
                file_.read(reinterpret_cast<char*>(&entry.dataSize), 4);
                file_.seekg(entry.dataSize, std::ios::cur);
            } else if (entry.packetType == TCV_PACKET_REF_FRAME) {
                file_.read(reinterpret_cast<char*>(&entry.refFrame), 4);
                entry.dataSize = 0;
            } else {
                tc::logWarning("TcvPlayer") << "Unknown packet type at frame " << i;
                break;
            }

            frameIndex_.push_back(entry);
        }

        tc::logNotice("TcvPlayer") << "Indexed " << frameIndex_.size() << " frames";
    }

    // Get or decode I-frame BC7 data
    const std::vector<uint8_t>& getIFrameData(int frameNum) {
        // Check cache
        auto it = iFrameCache_.find(frameNum);
        if (it != iFrameCache_.end()) {
            return it->second;
        }

        // Load from file
        const auto& entry = frameIndex_[frameNum];
        if (entry.packetType != TCV_PACKET_I_FRAME) {
            tc::logError("TcvPlayer") << "Frame " << frameNum << " is not an I-frame";
            static std::vector<uint8_t> empty;
            return empty;
        }

        file_.seekg(entry.offset);
        uint8_t packetType;
        uint32_t dataSize;
        file_.read(reinterpret_cast<char*>(&packetType), 1);
        file_.read(reinterpret_cast<char*>(&dataSize), 4);

        std::vector<uint8_t> data(dataSize);
        file_.read(reinterpret_cast<char*>(data.data()), dataSize);

        // Cache it (limit cache size to avoid memory issues)
        if (iFrameCache_.size() >= TCV_IFRAME_BUFFER_SIZE) {
            // Remove oldest entry (simple strategy)
            iFrameCache_.erase(iFrameCache_.begin());
        }
        iFrameCache_[frameNum] = std::move(data);

        return iFrameCache_[frameNum];
    }

    // Write a 16x16 block (16 4x4 blocks) to GPU layout buffer
    // bx16, by16: 16x16 block coordinates
    void writeBlockToGpuLayout(int bx16, int by16, const uint8_t* bc7Data) {
        int bc7BlocksX = blocksX_ * 4;  // 4x4 blocks per row

        for (int by4 = 0; by4 < 4; by4++) {
            for (int bx4 = 0; bx4 < 4; bx4++) {
                // GPU coordinates for this 4x4 block
                int gpuX = bx16 * 4 + bx4;
                int gpuY = by16 * 4 + by4;
                int gpuIdx = gpuY * bc7BlocksX + gpuX;

                // Copy 16 bytes (one BC7 block)
                std::memcpy(bc7Buffer_.data() + gpuIdx * 16,
                           bc7Data + (by4 * 4 + bx4) * 16,
                           16);
            }
        }
    }

    // Encode solid color to BC7 and write to GPU layout
    void writeSolidBlockToGpuLayout(int bx16, int by16, uint32_t color) {
        // Create a 4x4 block of solid color
        uint8_t block4x4[64];
        for (int i = 0; i < 16; i++) {
            std::memcpy(block4x4 + i * 4, &color, 4);
        }

        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        params.m_max_partitions = 0;
        params.m_uber_level = 0;

        int bc7BlocksX = blocksX_ * 4;

        // Encode and write each 4x4 block to correct GPU position
        for (int by4 = 0; by4 < 4; by4++) {
            for (int bx4 = 0; bx4 < 4; bx4++) {
                int gpuX = bx16 * 4 + bx4;
                int gpuY = by16 * 4 + by4;
                int gpuIdx = gpuY * bc7BlocksX + gpuX;

                bc7enc_compress_block(bc7Buffer_.data() + gpuIdx * 16, block4x4, &params);
            }
        }
    }

    // Decode a specific frame
    void decodeFrame(int frameNum) {
        if (frameNum < 0 || frameNum >= static_cast<int>(frameIndex_.size())) {
            return;
        }

        const auto& entry = frameIndex_[frameNum];

        if (entry.packetType == TCV_PACKET_I_FRAME) {
            // I-frame: load BC7 data directly (already in GPU layout)
            const auto& data = getIFrameData(frameNum);
            if (data.size() == bc7FrameSize_) {
                std::memcpy(bc7Buffer_.data(), data.data(), bc7FrameSize_);
            }
        } else if (entry.packetType == TCV_PACKET_REF_FRAME) {
            // REF-frame: copy from referenced I-frame
            const auto& refData = getIFrameData(entry.refFrame);
            if (refData.size() == bc7FrameSize_) {
                std::memcpy(bc7Buffer_.data(), refData.data(), bc7FrameSize_);
            }
        } else if (entry.packetType == TCV_PACKET_P_FRAME) {
            // P-frame: start with reference (GPU layout), apply deltas
            const auto& refData = getIFrameData(entry.refFrame);
            if (refData.size() == bc7FrameSize_) {
                std::memcpy(bc7Buffer_.data(), refData.data(), bc7FrameSize_);
            }

            // Read and apply block commands
            file_.seekg(entry.offset);
            uint8_t packetType;
            uint32_t refFrame, dataSize;
            file_.read(reinterpret_cast<char*>(&packetType), 1);
            file_.read(reinterpret_cast<char*>(&refFrame), 4);
            file_.read(reinterpret_cast<char*>(&dataSize), 4);

            int blockIdx = 0;
            while (blockIdx < totalBlocks_) {
                uint8_t cmd;
                file_.read(reinterpret_cast<char*>(&cmd), 1);

                uint8_t blockType = cmd & TCV_BLOCK_TYPE_MASK;
                int runLength = (cmd & TCV_BLOCK_RUN_MASK) + 1;

                for (int i = 0; i < runLength && blockIdx < totalBlocks_; i++, blockIdx++) {
                    int bx16 = blockIdx % blocksX_;
                    int by16 = blockIdx / blocksX_;

                    if (blockType == TCV_BLOCK_SKIP) {
                        // Keep reference data (already copied in GPU layout)
                    } else if (blockType == TCV_BLOCK_SOLID) {
                        uint32_t color;
                        file_.read(reinterpret_cast<char*>(&color), 4);
                        writeSolidBlockToGpuLayout(bx16, by16, color);
                    } else if (blockType == TCV_BLOCK_BC7) {
                        // Read 16x16 block data (256 bytes = 16 BC7 4x4 blocks)
                        uint8_t blockData[256];
                        file_.read(reinterpret_cast<char*>(blockData), 256);
                        writeBlockToGpuLayout(bx16, by16, blockData);
                    }
                }
            }
        }

        // Upload to GPU texture
        texture_.updateCompressed(bc7Buffer_.data(), bc7FrameSize_);
    }
};

} // namespace tcx
