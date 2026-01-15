#pragma once

// =============================================================================
// tcTcvPlayer.h - TCVC video player (v4: GPU layout, zero-copy decode)
// =============================================================================

#include <TrussC.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <chrono>

#include "tcTcvEncoder.h"  // For TcvHeader and constants
#include <lz4.h>           // For LZ4 decompression
#include <tc/sound/tcSound.h>  // For audio playback

namespace tcx {

// ---------------------------------------------------------------------------
// TcvPlayer - Plays TCVC encoded video
// ---------------------------------------------------------------------------
class TcvPlayer : public tc::VideoPlayerBase {
public:
    // Debug block types for visualization (v3: Skip or BC7 only)
    enum class DebugBlockType : uint8_t {
        None,    // I-frame or no debug
        Skip,    // Copy from reference
        BC7      // BC7 encoded
    };

    TcvPlayer() = default;
    ~TcvPlayer() { close(); }

    // Non-copyable
    TcvPlayer(const TcvPlayer&) = delete;
    TcvPlayer& operator=(const TcvPlayer&) = delete;

    // Debug mode
    void setDebug(bool enabled) { debugMode_ = enabled; }
    bool isDebug() const { return debugMode_; }

    // Performance stats
    double getAvgDecodeTimeMs() const {
        return decodeCount_ > 0 ? totalDecodeTimeMs_ / decodeCount_ : 0.0;
    }
    void resetStats() {
        totalDecodeTimeMs_ = 0.0;
        decodeCount_ = 0;
    }

    // Draw debug overlay (call after drawing video)
    void drawDebugOverlay(float x, float y, float scale = 1.0f) {
        if (!debugMode_ || debugBlockTypes_.empty()) return;

        float blockSize = TCV_BLOCK_SIZE * scale;

        tc::noFill();
        tc::setColor(1.0f, 0.0f, 0.0f, 0.8f);  // Red for BC7 blocks

        for (int by = 0; by < blocksY_; by++) {
            for (int bx = 0; bx < blocksX_; bx++) {
                int idx = by * blocksX_ + bx;
                // Only draw BC7 blocks (Skip blocks are unchanged from reference)
                if (debugBlockTypes_[idx] != DebugBlockType::BC7) continue;

                float rx = x + bx * blockSize;
                float ry = y + by * blockSize;
                tc::drawRect(rx + 0.5f, ry + 0.5f, blockSize - 1, blockSize - 1);
            }
        }

        tc::fill();
        tc::setColor(1.0f);
    }

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

        // Allocate LZ4 decompression buffers
        lz4CompressedBuffer_.resize(bc7FrameSize_ + 1024);
        lz4DecompressedBuffer_.resize(bc7FrameSize_ + 1024);

        // Allocate debug block types buffer
        debugBlockTypes_.resize(totalBlocks_, DebugBlockType::None);

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

        // Load audio if present
        hasAudio_ = false;
        if (header_.audioCodec != 0 && header_.audioSize > 0) {
            loadAudio();
        }

        tc::logNotice("TcvPlayer") << "Loaded: " << width_ << "x" << height_
                                   << " @ " << header_.fps << " fps, "
                                   << header_.frameCount << " frames"
                                   << (hasAudio_ ? " (with audio)" : "");
        return true;
    }

    void close() override {
        if (!initialized_) return;

        // Stop audio
        if (hasAudio_) {
            audio_.stop();
        }

        file_.close();
        texture_.clear();
        frameIndex_.clear();
        iFrameCache_.clear();
        bc7Buffer_.clear();
        debugBlockTypes_.clear();

        initialized_ = false;
        playing_ = false;
        paused_ = false;
        frameNew_ = false;
        firstFrameReceived_ = false;
        done_ = false;
        currentFrame_ = -1;
        hasAudio_ = false;
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

    // =========================================================================
    // Pixel access (not available - TCV uses GPU-compressed textures)
    // =========================================================================

    unsigned char* getPixels() override { return nullptr; }
    const unsigned char* getPixels() const override { return nullptr; }

protected:
    void playImpl() override {
        playStartTime_ = tc::getElapsedTimef();
        currentFrame_ = -1;

        // Start audio playback
        if (hasAudio_) {
            audio_.setPosition(0);
            audio_.play();
        }
    }

    void stopImpl() override {
        currentFrame_ = -1;

        // Stop audio
        if (hasAudio_) {
            audio_.stop();
        }
    }

    void setPausedImpl(bool paused) override {
        if (hasAudio_) {
            if (paused) {
                audio_.pause();
                pauseTime_ = tc::getElapsedTimef();
            } else {
                audio_.resume();
                // Adjust play start time for pause duration
                playStartTime_ += tc::getElapsedTimef() - pauseTime_;
            }
        }
    }

    void setPositionImpl(float pct) override {
        int frame = static_cast<int>(pct * header_.frameCount);
        setFrame(frame);
        playStartTime_ = tc::getElapsedTimef() - pct * getDuration();

        // Sync audio position
        if (hasAudio_) {
            audio_.setPosition(pct * audio_.getDuration());
        }
    }

    void setVolumeImpl(float vol) override {
        if (hasAudio_) {
            audio_.setVolume(vol);
        }
    }

    void setSpeedImpl(float speed) override {
        if (hasAudio_) {
            audio_.setSpeed(speed);
        }
    }

    void setLoopImpl(bool loop) override {
        if (hasAudio_) {
            audio_.setLoop(loop);
        }
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
    float pauseTime_ = 0.0f;

    // Audio playback
    bool hasAudio_ = false;
    tc::Sound audio_;

    // Frame index entry
    struct FrameIndexEntry {
        std::streampos offset;
        uint8_t packetType;
        uint32_t refFrame;       // For P/REF frames
        uint32_t dataSize;       // Uncompressed size
        uint32_t compressedSize; // LZ4 compressed size
    };
    std::vector<FrameIndexEntry> frameIndex_;

    // I-frame cache: frameNumber -> BC7 data
    std::unordered_map<int, std::vector<uint8_t>> iFrameCache_;

    // LZ4 decompression buffers
    std::vector<char> lz4CompressedBuffer_;
    std::vector<char> lz4DecompressedBuffer_;

    // Debug mode
    bool debugMode_ = false;
    std::vector<DebugBlockType> debugBlockTypes_;

    // Performance stats
    double totalDecodeTimeMs_ = 0.0;
    int decodeCount_ = 0;

    // Build index of frame offsets for seeking
    void buildFrameIndex() {
        frameIndex_.clear();
        frameIndex_.reserve(header_.frameCount);
        iFrameCache_.clear();

        file_.seekg(header_.headerSize);

        for (uint32_t i = 0; i < header_.frameCount; i++) {
            FrameIndexEntry entry;
            entry.offset = file_.tellg();
            entry.compressedSize = 0;

            file_.read(reinterpret_cast<char*>(&entry.packetType), 1);

            if (entry.packetType == TCV_PACKET_I_FRAME) {
                // v3: I-frame always LZ4 [type][dataSize][compSize][data]
                file_.read(reinterpret_cast<char*>(&entry.dataSize), 4);
                file_.read(reinterpret_cast<char*>(&entry.compressedSize), 4);
                entry.refFrame = 0;
                file_.seekg(entry.compressedSize, std::ios::cur);
            } else if (entry.packetType == TCV_PACKET_P_FRAME) {
                // v3: P-frame always LZ4 [type][refFrame][dataSize][compSize][data]
                file_.read(reinterpret_cast<char*>(&entry.refFrame), 4);
                file_.read(reinterpret_cast<char*>(&entry.dataSize), 4);
                file_.read(reinterpret_cast<char*>(&entry.compressedSize), 4);
                file_.seekg(entry.compressedSize, std::ios::cur);
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

    // Get or decode I-frame BC7 data (returns GPU layout BC7 buffer)
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

        // Allocate result buffer (full GPU layout BC7 data)
        std::vector<uint8_t> bc7Result(bc7FrameSize_);

        // Seek to frame data and skip header
        file_.seekg(entry.offset);
        uint8_t packetType;
        file_.read(reinterpret_cast<char*>(&packetType), 1);

        // Read sizes and decompress (v3: always LZ4)
        uint32_t uncompressedSize, compressedSize;
        file_.read(reinterpret_cast<char*>(&uncompressedSize), 4);
        file_.read(reinterpret_cast<char*>(&compressedSize), 4);

        file_.read(lz4CompressedBuffer_.data(), compressedSize);
        int decompressed = LZ4_decompress_safe(
            lz4CompressedBuffer_.data(),
            lz4DecompressedBuffer_.data(),
            static_cast<int>(compressedSize),
            static_cast<int>(lz4DecompressedBuffer_.size())
        );
        if (decompressed != static_cast<int>(uncompressedSize)) {
            tc::logError("TcvPlayer") << "LZ4 decompression failed for I-frame " << frameNum;
            static std::vector<uint8_t> empty;
            return empty;
        }

        // Parse BC7 block data from buffer
        const uint8_t* blockData = reinterpret_cast<const uint8_t*>(lz4DecompressedBuffer_.data());
        decodeIFrameFromBuffer(blockData, bc7Result.data());

        // Cache it (limit cache size to avoid memory issues)
        if (iFrameCache_.size() >= TCV_IFRAME_BUFFER_SIZE) {
            iFrameCache_.erase(iFrameCache_.begin());
        }
        iFrameCache_[frameNum] = std::move(bc7Result);

        return iFrameCache_[frameNum];
    }

    // Decode I-frame BC7 blocks from buffer (v4: already GPU layout, just memcpy)
    void decodeIFrameFromBuffer(const uint8_t* data, uint8_t* bc7Output) {
        // v4: Data is already in GPU layout (4x4 blocks row-major)
        std::memcpy(bc7Output, data, bc7FrameSize_);
    }

    // Copy a 16x16 block (256 bytes) directly to GPU buffer position
    // v4: Data is already in GPU layout order within the 256-byte block
    void copyBlockToGpuPosition(int bx16, int by16, const uint8_t* bc7Data) {
        int bc7BlocksX = blocksX_ * 4;  // 4x4 blocks per row

        for (int by4 = 0; by4 < 4; by4++) {
            for (int bx4 = 0; bx4 < 4; bx4++) {
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

    // Decode a specific frame
    void decodeFrame(int frameNum) {
        auto startTime = std::chrono::high_resolution_clock::now();

        if (frameNum < 0 || frameNum >= static_cast<int>(frameIndex_.size())) {
            return;
        }

        const auto& entry = frameIndex_[frameNum];

        // Clear debug block types (None = no overlay for I-frames)
        if (debugMode_) {
            std::fill(debugBlockTypes_.begin(), debugBlockTypes_.end(), DebugBlockType::None);
        }

        if (entry.packetType == TCV_PACKET_I_FRAME) {
            // I-frame: upload directly from cache (no intermediate copy)
            const auto& iData = getIFrameData(frameNum);
            if (iData.size() == bc7FrameSize_) {
                texture_.updateCompressed(iData.data(), bc7FrameSize_);
                // Record decode time and return early
                auto endTime = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();
                totalDecodeTimeMs_ += ms;
                decodeCount_++;
                return;
            }
        } else if (entry.packetType == TCV_PACKET_REF_FRAME) {
            // REF-frame: upload directly from referenced I-frame cache
            const auto& refData = getIFrameData(entry.refFrame);
            if (refData.size() == bc7FrameSize_) {
                texture_.updateCompressed(refData.data(), bc7FrameSize_);
                // For debug: REF-frame = all Skip
                if (debugMode_) {
                    std::fill(debugBlockTypes_.begin(), debugBlockTypes_.end(), DebugBlockType::Skip);
                }
                // Record decode time and return early
                auto endTime = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();
                totalDecodeTimeMs_ += ms;
                decodeCount_++;
                return;
            }
        } else if (entry.packetType == TCV_PACKET_P_FRAME) {
            // P-frame: start with reference (GPU layout), apply deltas
            const auto& refData = getIFrameData(entry.refFrame);
            if (refData.size() == bc7FrameSize_) {
                std::memcpy(bc7Buffer_.data(), refData.data(), bc7FrameSize_);
            }

            // Read and decompress block commands (v3: always LZ4)
            file_.seekg(entry.offset);
            uint8_t packetType;
            uint32_t refFrame;
            uint32_t uncompressedSize, compressedSize;
            file_.read(reinterpret_cast<char*>(&packetType), 1);
            file_.read(reinterpret_cast<char*>(&refFrame), 4);
            file_.read(reinterpret_cast<char*>(&uncompressedSize), 4);
            file_.read(reinterpret_cast<char*>(&compressedSize), 4);

            file_.read(lz4CompressedBuffer_.data(), compressedSize);
            int decompressed = LZ4_decompress_safe(
                lz4CompressedBuffer_.data(),
                lz4DecompressedBuffer_.data(),
                static_cast<int>(compressedSize),
                static_cast<int>(lz4DecompressedBuffer_.size())
            );
            if (decompressed != static_cast<int>(uncompressedSize)) {
                tc::logError("TcvPlayer") << "LZ4 decompression failed for P-frame " << frameNum;
                return;
            }

            const uint8_t* blockData = reinterpret_cast<const uint8_t*>(lz4DecompressedBuffer_.data());

            // Parse block commands (v3: SKIP or BC7 only)
            size_t offset = 0;
            int blockIdx = 0;
            while (blockIdx < totalBlocks_) {
                uint8_t cmd = blockData[offset++];

                uint8_t blockType = cmd & TCV_BLOCK_TYPE_MASK;
                int runLength = (cmd & TCV_BLOCK_RUN_MASK) + 1;

                for (int i = 0; i < runLength && blockIdx < totalBlocks_; i++, blockIdx++) {
                    int bx16 = blockIdx % blocksX_;
                    int by16 = blockIdx / blocksX_;

                    if (blockType == TCV_BLOCK_SKIP) {
                        // Keep reference data (already copied in GPU layout)
                        if (debugMode_) debugBlockTypes_[blockIdx] = DebugBlockType::Skip;
                    } else {  // TCV_BLOCK_BC7
                        copyBlockToGpuPosition(bx16, by16, blockData + offset);
                        offset += 256;
                        if (debugMode_) debugBlockTypes_[blockIdx] = DebugBlockType::BC7;
                    }
                }
            }
        }

        // Upload to GPU texture
        texture_.updateCompressed(bc7Buffer_.data(), bc7FrameSize_);

        // Record decode time
        auto endTime = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        totalDecodeTimeMs_ += ms;
        decodeCount_++;
    }

    // Load audio from TCV file
    void loadAudio() {
        if (header_.audioOffset == 0 || header_.audioSize == 0) {
            return;
        }

        // Read audio data from file
        std::vector<uint8_t> audioData(header_.audioSize);
        file_.seekg(static_cast<std::streamoff>(header_.audioOffset));
        file_.read(reinterpret_cast<char*>(audioData.data()), header_.audioSize);

        // Create SoundBuffer and decode based on codec
        auto soundBuffer = std::make_shared<tc::SoundBuffer>();
        bool loaded = false;

        // Check codec FourCC (big-endian: MSB is first character)
        uint32_t codec = header_.audioCodec;
        char codecStr[5] = {0};
        codecStr[0] = static_cast<char>((codec >> 24) & 0xFF);
        codecStr[1] = static_cast<char>((codec >> 16) & 0xFF);
        codecStr[2] = static_cast<char>((codec >> 8) & 0xFF);
        codecStr[3] = static_cast<char>(codec & 0xFF);

        // FourCC values (big-endian)
        // kAudioFormatMPEGLayer3 = '.mp3' = 0x2E6D7033
        constexpr uint32_t FOURCC_MP3_DOT = 0x2E6D7033;  // '.mp3' (kAudioFormatMPEGLayer3)
        constexpr uint32_t FOURCC_MP3_SPC = 0x6D703320;  // 'mp3 '
        constexpr uint32_t FOURCC_AAC     = 0x61616320;  // 'aac ' (kAudioFormatMPEG4AAC)
        constexpr uint32_t FOURCC_MP4A    = 0x6D703461;  // 'mp4a'

        // Debug: show codec value
        tc::logNotice("TcvPlayer") << "Audio codec: " << codecStr
                                   << " (0x" << std::hex << codec << std::dec << ")"
                                   << ", data size: " << audioData.size() << " bytes";

        if (codec == FOURCC_MP3_DOT || codec == FOURCC_MP3_SPC) {
            // MP3: decode from memory
            tc::logNotice("TcvPlayer") << "Attempting MP3 decode...";
            loaded = soundBuffer->loadMp3FromMemory(audioData.data(), audioData.size());
            if (loaded) {
                tc::logNotice("TcvPlayer") << "MP3 decode successful: "
                                           << soundBuffer->channels << " ch, "
                                           << soundBuffer->sampleRate << " Hz, "
                                           << soundBuffer->numSamples << " samples";
            } else {
                tc::logError("TcvPlayer") << "MP3 decode failed!";
            }
        } else if (codec == FOURCC_AAC || codec == FOURCC_MP4A) {
            // AAC: decode using platform-specific decoder
            tc::logNotice("TcvPlayer") << "Attempting AAC decode...";
            loaded = soundBuffer->loadAacFromMemory(audioData.data(), audioData.size());
            if (loaded) {
                tc::logNotice("TcvPlayer") << "AAC decode successful: "
                                           << soundBuffer->channels << " ch, "
                                           << soundBuffer->sampleRate << " Hz, "
                                           << soundBuffer->numSamples << " samples";
            } else {
                tc::logWarning("TcvPlayer") << "AAC decode failed (may not be supported on this platform)";
            }
        } else {
            tc::logWarning("TcvPlayer") << "Unknown audio codec - cannot decode";
        }

        if (loaded) {
            audio_.loadFromBuffer(soundBuffer);
            hasAudio_ = true;
            tc::logNotice("TcvPlayer") << "Audio loaded: " << audioData.size() << " bytes, "
                                       << soundBuffer->getDuration() << "s duration";
        }
    }
};

} // namespace tcx
