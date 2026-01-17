#pragma once

// =============================================================================
// tcTcvEncoder.h - TCVC video encoder (v4: GPU layout, parallel encoding)
// =============================================================================

#include <TrussC.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <thread>
#include <atomic>
#include <array>

// BC7 encoder/decoder
#include "impl/bc7enc.h"
#include "impl/bcdec.h"

// LZ4 compression
#include <lz4.h>

namespace tcx {

// TCVC File Format Constants
// ---------------------------------------------------------------------------
constexpr uint32_t TCV_SIGNATURE = 0x56435654;  // "TCVC" little-endian
constexpr uint16_t TCV_VERSION = 5;             // v5: Chunked LZ4 for parallel decode
constexpr uint16_t TCV_HEADER_SIZE = 64;
constexpr uint16_t TCV_BLOCK_SIZE = 16;         // 16x16 pixel blocks

// Packet types
constexpr uint8_t TCV_PACKET_I_FRAME   = 0x01;  // I-frame: BC7 block data + chunked LZ4
constexpr uint8_t TCV_PACKET_P_FRAME   = 0x02;  // P-frame: SKIP/BC7 commands + LZ4
constexpr uint8_t TCV_PACKET_REF_FRAME = 0x03;  // REF-frame: exact duplicate reference

// Block command types (bit 7 of command byte)
constexpr uint8_t TCV_BLOCK_SKIP    = 0x00;     // Same as reference frame (0xxxxxxx)
constexpr uint8_t TCV_BLOCK_BC7     = 0x80;     // BC7 encoded (1xxxxxxx)
constexpr uint8_t TCV_BLOCK_TYPE_MASK = 0x80;   // Mask for type bit
constexpr uint8_t TCV_BLOCK_RUN_MASK  = 0x7F;   // Mask for run length (0-127 = 1-128)

// I-frame buffer size
constexpr int TCV_IFRAME_BUFFER_SIZE = 10;

// LZ4 chunk settings (v5)
constexpr size_t TCV_MIN_CHUNK_SIZE = 128 * 1024;  // 128KB minimum per chunk
constexpr int TCV_MAX_CHUNKS = 16;                  // Encoder limit (decoder accepts any)

// ---------------------------------------------------------------------------
// TCVC Header Structure (64 bytes)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct TcvHeader {
    char     signature[4];    // 0x00: "TCVC"
    uint16_t version;         // 0x04: 2
    uint16_t headerSize;      // 0x06: 64
    uint32_t width;           // 0x08
    uint32_t height;          // 0x0C
    uint32_t frameCount;      // 0x10
    float    fps;             // 0x14
    uint16_t blockSize;       // 0x18: 16
    uint16_t reserved1;       // 0x1A
    uint32_t reserved2;       // 0x1C
    uint32_t reserved3;       // 0x20
    uint32_t audioCodec;      // 0x24: FourCC (0=none)
    uint32_t audioSampleRate; // 0x28: Sample rate (Hz)
    uint32_t audioChannels;   // 0x2C: Channel count
    uint64_t audioOffset;     // 0x30
    uint64_t audioSize;       // 0x38
};
#pragma pack(pop)

static_assert(sizeof(TcvHeader) == 64, "TcvHeader must be 64 bytes");

// ---------------------------------------------------------------------------
// TcvEncoder - Encodes video to TCVC format
// ---------------------------------------------------------------------------
class TcvEncoder {
public:
    TcvEncoder() {
        bc7enc_compress_block_init();
    }

    ~TcvEncoder() {
        if (isEncoding_) {
            end();
        }
    }

    // Non-copyable
    TcvEncoder(const TcvEncoder&) = delete;
    TcvEncoder& operator=(const TcvEncoder&) = delete;

    // =========================================================================
    // Encoding API
    // =========================================================================

    bool begin(const std::string& path, int width, int height, float fps) {
        if (isEncoding_) {
            tc::logError("TcvEncoder") << "Already encoding";
            return false;
        }

        if (width <= 0 || height <= 0) {
            tc::logError("TcvEncoder") << "Invalid dimensions: " << width << "x" << height;
            return false;
        }

        file_.open(path, std::ios::binary);
        if (!file_.is_open()) {
            tc::logError("TcvEncoder") << "Failed to open file: " << path;
            return false;
        }

        width_ = width;
        height_ = height;
        fps_ = fps;
        frameCount_ = 0;

        // Calculate block counts (round up)
        blocksX_ = (width + TCV_BLOCK_SIZE - 1) / TCV_BLOCK_SIZE;
        blocksY_ = (height + TCV_BLOCK_SIZE - 1) / TCV_BLOCK_SIZE;
        totalBlocks_ = blocksX_ * blocksY_;

        // Write placeholder header
        TcvHeader header = {};
        std::memcpy(header.signature, "TCVC", 4);
        header.version = TCV_VERSION;
        header.headerSize = TCV_HEADER_SIZE;
        header.width = width;
        header.height = height;
        header.frameCount = 0;
        header.fps = fps;
        header.blockSize = TCV_BLOCK_SIZE;
        header.audioCodec = 0;
        header.audioSampleRate = 0;
        header.audioChannels = 0;
        header.audioOffset = 0;
        header.audioSize = 0;

        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // Allocate buffers
        paddedWidth_ = blocksX_ * TCV_BLOCK_SIZE;
        paddedHeight_ = blocksY_ * TCV_BLOCK_SIZE;
        size_t pixelBufferSize = paddedWidth_ * paddedHeight_ * 4;
        size_t bc7BufferSize = blocksX_ * blocksY_ * 256;  // 16 BC7 blocks per 16x16

        paddedPixels_.resize(pixelBufferSize, 0);
        bc7Buffer_.resize(bc7BufferSize);
        framePacketBuffer_.reserve(bc7BufferSize + 1024);  // Extra for commands

        // Initialize I-frame buffer
        for (auto& entry : iFrameBuffer_) {
            entry.frameNumber = -1;
            entry.pixels.resize(pixelBufferSize, 0);
            entry.bc7Data.resize(bc7BufferSize, 0);
            entry.hash = 0;
        }
        iFrameBufferHead_ = 0;
        iFrameBufferCount_ = 0;
        lastIFrameNumber_ = -1;

        // Reset stats
        statIFrames_ = 0;
        statPFrames_ = 0;
        statRefFrames_ = 0;
        statSkipBlocks_ = 0;
        statBc7Blocks_ = 0;

        // Initialize LZ4 buffer (worst case: slightly larger than input)
        lz4Buffer_.resize(LZ4_compressBound(static_cast<int>(bc7BufferSize + 1024)));

        isEncoding_ = true;

        int actualThreads = (numThreads_ > 0) ? numThreads_ : std::thread::hardware_concurrency();
        if (actualThreads < 1) actualThreads = 1;

        tc::logNotice("TcvEncoder") << "Started encoding: " << width << "x" << height
                                    << " @ " << fps << " fps (" << actualThreads << " threads)";

        if (forceAllIFrames_) {
            tc::logNotice("TcvEncoder") << "Mode: All I-frames";
        } else {
            tc::logNotice("TcvEncoder") << "Mode: I/P frames + LZ4";
        }

        return true;
    }

    bool addFrame(const unsigned char* rgbaPixels) {
        if (!isEncoding_) {
            tc::logError("TcvEncoder") << "Not encoding";
            return false;
        }

        // Copy to padded buffer
        copyToPadded(rgbaPixels);

        // Calculate hash for this frame
        uint64_t frameHash = computeHash(paddedPixels_.data(), paddedPixels_.size());

        // Decide frame type: I, P, or REF
        enum class FrameType { I, P, Ref };
        FrameType frameType = FrameType::I;
        int refFrameNum = -1;
        std::vector<BlockType> blockTypes;

        if (forceAllIFrames_) {
            // Option: force all I-frames
            frameType = FrameType::I;
        } else if (iFrameBufferCount_ == 0) {
            // First frame must be I-frame
            frameType = FrameType::I;
        } else {
            // Try to find best reference frame
            int bestRefIndex = -1;
            int bestRefFrameNum = -1;
            int bestMatchCount = -1;
            std::vector<BlockType> bestBlockTypes;
            bool exactMatch = false;

            // Check each I-frame in buffer
            for (int i = 0; i < iFrameBufferCount_; i++) {
                int idx = (iFrameBufferHead_ - 1 - i + TCV_IFRAME_BUFFER_SIZE) % TCV_IFRAME_BUFFER_SIZE;
                const auto& entry = iFrameBuffer_[idx];

                // Hash match = potential exact duplicate
                if (entry.hash == frameHash) {
                    // Verify pixels actually match (hash collision check)
                    if (std::memcmp(paddedPixels_.data(), entry.pixels.data(), paddedPixels_.size()) == 0) {
                        // Exact match - use REF_FRAME
                        bestRefIndex = idx;
                        bestRefFrameNum = entry.frameNumber;
                        exactMatch = true;
                        break;
                    }
                }

                // Compare blocks
                std::vector<BlockType> curBlockTypes(totalBlocks_);
                int matchCount = 0;

                for (int by = 0; by < blocksY_; by++) {
                    for (int bx = 0; bx < blocksX_; bx++) {
                        int blockIdx = by * blocksX_ + bx;
                        BlockType type = analyzeBlock(bx, by, entry.pixels.data());
                        curBlockTypes[blockIdx] = type;
                        if (type == BlockType::Skip) {
                            matchCount++;
                        }
                    }
                }

                if (matchCount > bestMatchCount) {
                    bestMatchCount = matchCount;
                    bestRefIndex = idx;
                    bestRefFrameNum = entry.frameNumber;
                    bestBlockTypes = std::move(curBlockTypes);
                }
            }

            if (exactMatch) {
                // Exact duplicate - REF_FRAME
                frameType = FrameType::Ref;
                refFrameNum = bestRefFrameNum;
            } else if (bestRefIndex >= 0) {
                // Calculate BC7 ratio
                int bc7Count = totalBlocks_ - bestMatchCount;
                float bc7Ratio = static_cast<float>(bc7Count) / totalBlocks_;

                // Adaptive threshold based on frames since last I
                int framesSinceI = frameCount_ - lastIFrameNumber_;
                float threshold;
                if (framesSinceI < 30) {
                    threshold = 0.50f;
                } else if (framesSinceI < 60) {
                    threshold = 0.05f;
                } else if (framesSinceI < 120) {
                    threshold = 0.01f;
                } else {
                    threshold = 0.0f;  // Force I-frame
                }

                if (bc7Ratio > threshold) {
                    frameType = FrameType::I;
                } else {
                    frameType = FrameType::P;
                    refFrameNum = bestRefFrameNum;
                    blockTypes = std::move(bestBlockTypes);
                }
            } else {
                frameType = FrameType::I;
            }
        }

        // Encode the frame
        switch (frameType) {
            case FrameType::I:
                encodeIFrame();
                break;
            case FrameType::P:
                encodePFrame(refFrameNum, blockTypes);
                break;
            case FrameType::Ref:
                encodeRefFrame(refFrameNum);
                break;
        }

        frameCount_++;
        return true;
    }

    // Set audio data to embed (call before end())
    void setAudio(const std::vector<uint8_t>& data, uint32_t codec, int sampleRate = 0, int channels = 0) {
        audioData_ = data;
        audioCodec_ = codec;
        audioSampleRate_ = sampleRate;
        audioChannels_ = channels;
    }

    void setAudio(std::vector<uint8_t>&& data, uint32_t codec, int sampleRate = 0, int channels = 0) {
        audioData_ = std::move(data);
        audioCodec_ = codec;
        audioSampleRate_ = sampleRate;
        audioChannels_ = channels;
    }

    bool end() {
        if (!isEncoding_) {
            return false;
        }

        // Write audio data if present
        uint64_t audioOffset = 0;
        uint64_t audioSize = 0;
        if (!audioData_.empty() && audioCodec_ != 0) {
            audioOffset = static_cast<uint64_t>(file_.tellp());
            audioSize = audioData_.size();
            file_.write(reinterpret_cast<const char*>(audioData_.data()), audioSize);

            tc::logNotice("TcvEncoder") << "Audio embedded: " << audioSize << " bytes"
                                        << " (codec: " << codecToString(audioCodec_) << ")";
        }

        // Update header with actual frame count and audio info
        file_.seekp(0x10);
        file_.write(reinterpret_cast<const char*>(&frameCount_), 4);

        // Audio codec at 0x24
        file_.seekp(0x24);
        file_.write(reinterpret_cast<const char*>(&audioCodec_), 4);

        // Audio sample rate at 0x28
        uint32_t sampleRate = static_cast<uint32_t>(audioSampleRate_);
        file_.seekp(0x28);
        file_.write(reinterpret_cast<const char*>(&sampleRate), 4);

        // Audio channels at 0x2C
        uint32_t channels = static_cast<uint32_t>(audioChannels_);
        file_.seekp(0x2C);
        file_.write(reinterpret_cast<const char*>(&channels), 4);

        // Audio offset at 0x30
        file_.seekp(0x30);
        file_.write(reinterpret_cast<const char*>(&audioOffset), 8);

        // Audio size at 0x38
        file_.seekp(0x38);
        file_.write(reinterpret_cast<const char*>(&audioSize), 8);

        file_.close();
        isEncoding_ = false;

        // Clear audio buffer
        audioData_.clear();
        audioCodec_ = 0;

        // Print stats
        tc::logNotice("TcvEncoder") << "=== Encoding Complete ===";
        tc::logNotice("TcvEncoder") << "Frames: " << frameCount_
                                    << " (I: " << statIFrames_
                                    << ", P: " << statPFrames_
                                    << ", REF: " << statRefFrames_ << ")";
        if (statPFrames_ > 0) {
            uint64_t totalPBlocks = statSkipBlocks_ + statBc7Blocks_;
            tc::logNotice("TcvEncoder") << "P-frame blocks: SKIP=" << statSkipBlocks_
                                        << " (" << (100.0f * statSkipBlocks_ / totalPBlocks) << "%)"
                                        << ", BC7=" << statBc7Blocks_
                                        << " (" << (100.0f * statBc7Blocks_ / totalPBlocks) << "%)";
        }

        return true;
    }

    // =========================================================================
    // Settings
    // =========================================================================

    bool isEncoding() const { return isEncoding_; }
    uint32_t getFrameCount() const { return frameCount_; }

    // Quality: 0=fast, 1=balanced, 2=high
    void setQuality(int quality) { quality_ = std::clamp(quality, 0, 2); }

    // Manual BC7 parameters (overrides quality preset if >= 0)
    void setPartitions(int partitions) { partitions_ = partitions; }
    void setUberLevel(int uber) { uber_ = uber; }

    // Thread count (0 = auto)
    void setThreadCount(int numThreads) {
        numThreads_ = numThreads;
        if (numThreads_ < 0) numThreads_ = 0;
    }

    // === Compression options ===

    // Force all frames to be I-frames (no P-frames)
    void setForceAllIFrames(bool force) { forceAllIFrames_ = force; }

    // Enable/disable SKIP blocks (for benchmarking)
    void setEnableSkip(bool enable) { enableSkip_ = enable; }

    // Max LZ4 chunks for I-frame parallel decode (1-16)
    void setMaxChunks(int maxChunks) { maxChunks_ = std::clamp(maxChunks, 1, 16); }

private:
    // File output
    std::ofstream file_;
    bool isEncoding_ = false;

    // Encoding settings
    int quality_ = 1;
    int partitions_ = -1;
    int uber_ = -1;
    int numThreads_ = 0;
    int maxChunks_ = TCV_MAX_CHUNKS;  // Default 16

    // Compression options
    bool forceAllIFrames_ = false;
    bool enableSkip_ = true;

    // Audio data to embed
    std::vector<uint8_t> audioData_;
    uint32_t audioCodec_ = 0;
    int audioSampleRate_ = 0;
    int audioChannels_ = 0;

    // LZ4 compression buffer
    std::vector<char> lz4Buffer_;

    // Video properties
    int width_ = 0;
    int height_ = 0;
    float fps_ = 0;
    uint32_t frameCount_ = 0;

    // Block layout
    int blocksX_ = 0;
    int blocksY_ = 0;
    int totalBlocks_ = 0;
    int paddedWidth_ = 0;
    int paddedHeight_ = 0;

    // Buffers
    std::vector<uint8_t> paddedPixels_;
    std::vector<uint8_t> bc7Buffer_;
    std::vector<uint8_t> framePacketBuffer_;

    // I-frame ring buffer
    struct IFrameEntry {
        int frameNumber = -1;
        std::vector<uint8_t> pixels;
        std::vector<uint8_t> bc7Data;
        uint64_t hash = 0;
    };
    std::array<IFrameEntry, TCV_IFRAME_BUFFER_SIZE> iFrameBuffer_;
    int iFrameBufferHead_ = 0;
    int iFrameBufferCount_ = 0;
    int lastIFrameNumber_ = -1;

    // Block types (v3: simplified to Skip and BC7 only)
    enum class BlockType { Skip, BC7 };

    // Stats
    uint64_t statIFrames_ = 0;
    uint64_t statPFrames_ = 0;
    uint64_t statRefFrames_ = 0;
    uint64_t statSkipBlocks_ = 0;
    uint64_t statBc7Blocks_ = 0;

    bc7enc_compress_block_params bc7Params_;

    // =========================================================================
    // Helper functions
    // =========================================================================

    static std::string codecToString(uint32_t fourcc) {
        if (fourcc == 0) return "none";
        char str[5] = {0};
        // FourCC is big-endian: most significant byte is first character
        str[0] = static_cast<char>((fourcc >> 24) & 0xFF);
        str[1] = static_cast<char>((fourcc >> 16) & 0xFF);
        str[2] = static_cast<char>((fourcc >> 8) & 0xFF);
        str[3] = static_cast<char>(fourcc & 0xFF);
        return str;
    }

    void copyToPadded(const unsigned char* src) {
        std::fill(paddedPixels_.begin(), paddedPixels_.end(), 0);
        for (int y = 0; y < height_; y++) {
            const unsigned char* srcRow = src + y * width_ * 4;
            unsigned char* dstRow = paddedPixels_.data() + y * paddedWidth_ * 4;
            std::memcpy(dstRow, srcRow, width_ * 4);
        }
    }

    // FNV-1a hash
    uint64_t computeHash(const uint8_t* data, size_t size) {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < size; i++) {
            hash ^= data[i];
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    // Analyze a 16x16 block: returns Skip if identical to reference, BC7 otherwise
    BlockType analyzeBlock(int bx, int by, const uint8_t* refPixels) {
        if (!enableSkip_) return BlockType::BC7;

        int startX = bx * TCV_BLOCK_SIZE;
        int startY = by * TCV_BLOCK_SIZE;

        const uint8_t* curBlock = paddedPixels_.data();
        const uint8_t* refBlock = refPixels;

        // Compare with reference
        for (int py = 0; py < TCV_BLOCK_SIZE; py++) {
            int y = startY + py;
            const uint8_t* curRow = curBlock + (y * paddedWidth_ + startX) * 4;
            const uint8_t* refRow = refBlock + (y * paddedWidth_ + startX) * 4;
            if (std::memcmp(curRow, refRow, TCV_BLOCK_SIZE * 4) != 0) {
                return BlockType::BC7;
            }
        }
        return BlockType::Skip;
    }

    // Encode a 16x16 block to BC7 (16 4x4 blocks = 256 bytes)
    void encodeBlockToBC7(int bx, int by, uint8_t* outBC7) {
        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);

        switch (quality_) {
            case 0: params.m_max_partitions = 0; params.m_uber_level = 0; break;
            case 1: params.m_max_partitions = 16; params.m_uber_level = 1; break;
            case 2: default: params.m_max_partitions = 64; params.m_uber_level = 4; break;
        }
        if (partitions_ >= 0) params.m_max_partitions = std::clamp(partitions_, 0, 64);
        if (uber_ >= 0) params.m_uber_level = std::clamp(uber_, 0, 4);

        int startX = bx * TCV_BLOCK_SIZE;
        int startY = by * TCV_BLOCK_SIZE;
        uint8_t block4x4[64];

        // 16x16 = 4x4 grid of 4x4 BC7 blocks
        for (int by4 = 0; by4 < 4; by4++) {
            for (int bx4 = 0; bx4 < 4; bx4++) {
                int x = startX + bx4 * 4;
                int y = startY + by4 * 4;

                // Extract 4x4 pixels
                for (int py = 0; py < 4; py++) {
                    const uint8_t* srcRow = paddedPixels_.data() + ((y + py) * paddedWidth_ + x) * 4;
                    std::memcpy(block4x4 + py * 16, srcRow, 16);
                }

                // Encode BC7 block
                bc7enc_compress_block(outBC7 + (by4 * 4 + bx4) * 16, block4x4, &params);
            }
        }
    }

    // Encode all blocks to BC7 in GPU layout (4x4 blocks row-major)
    void encodeAllBlocksToBC7() {
        int actualThreads = (numThreads_ > 0) ? numThreads_ : std::thread::hardware_concurrency();
        if (actualThreads < 1) actualThreads = 1;

        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        switch (quality_) {
            case 0: params.m_max_partitions = 0; params.m_uber_level = 0; break;
            case 1: params.m_max_partitions = 16; params.m_uber_level = 1; break;
            case 2: default: params.m_max_partitions = 64; params.m_uber_level = 4; break;
        }
        if (partitions_ >= 0) params.m_max_partitions = std::clamp(partitions_, 0, 64);
        if (uber_ >= 0) params.m_uber_level = std::clamp(uber_, 0, 4);

        int bc7BlocksX = paddedWidth_ / 4;  // Number of 4x4 blocks in X
        int bc7BlocksY = paddedHeight_ / 4; // Number of 4x4 blocks in Y
        int totalBC7Blocks = bc7BlocksX * bc7BlocksY;

        auto processBlocks = [&](int startBlock, int endBlock) {
            bc7enc_compress_block_params localParams = params;
            uint8_t block4x4[64];

            for (int bc7Idx = startBlock; bc7Idx < endBlock; bc7Idx++) {
                int bx = bc7Idx % bc7BlocksX;
                int by = bc7Idx / bc7BlocksX;
                int x = bx * 4;
                int y = by * 4;

                // Extract 4x4 pixels
                for (int py = 0; py < 4; py++) {
                    const uint8_t* srcRow = paddedPixels_.data() + ((y + py) * paddedWidth_ + x) * 4;
                    std::memcpy(block4x4 + py * 16, srcRow, 16);
                }

                // Encode to GPU layout position
                bc7enc_compress_block(bc7Buffer_.data() + bc7Idx * 16, block4x4, &localParams);
            }
        };

        if (actualThreads == 1) {
            processBlocks(0, totalBC7Blocks);
        } else {
            std::vector<std::thread> threads;
            int blocksPerThread = totalBC7Blocks / actualThreads;
            int remainder = totalBC7Blocks % actualThreads;
            int current = 0;

            for (int i = 0; i < actualThreads; i++) {
                int count = blocksPerThread + (i < remainder ? 1 : 0);
                if (count > 0) {
                    threads.emplace_back(processBlocks, current, current + count);
                    current += count;
                }
            }

            for (auto& t : threads) {
                t.join();
            }
        }
    }

    // =========================================================================
    // Frame encoding
    // =========================================================================

    void encodeIFrame() {
        // Encode all blocks to BC7 (multi-threaded)
        encodeAllBlocksToBC7();

        // Calculate chunk count based on data size and maxChunks_ setting
        uint32_t dataSize = static_cast<uint32_t>(bc7Buffer_.size());
        int chunkCount = std::max(1, std::min(maxChunks_,
            static_cast<int>(dataSize / TCV_MIN_CHUNK_SIZE)));
        size_t chunkSize = dataSize / chunkCount;

        // Compress each chunk with LZ4
        std::vector<uint32_t> chunkCompressedSizes(chunkCount);
        std::vector<std::vector<char>> chunkBuffers(chunkCount);

        for (int i = 0; i < chunkCount; i++) {
            size_t offset = i * chunkSize;
            size_t thisChunkSize = (i == chunkCount - 1)
                ? (dataSize - offset)  // Last chunk gets remainder
                : chunkSize;

            // Allocate buffer for compressed data
            chunkBuffers[i].resize(LZ4_compressBound(static_cast<int>(thisChunkSize)));

            int compressed = LZ4_compress_default(
                reinterpret_cast<const char*>(bc7Buffer_.data() + offset),
                chunkBuffers[i].data(),
                static_cast<int>(thisChunkSize),
                static_cast<int>(chunkBuffers[i].size())
            );
            chunkCompressedSizes[i] = static_cast<uint32_t>(compressed);
        }

        // Write I-frame packet (v5 chunked format)
        uint8_t packetType = TCV_PACKET_I_FRAME;
        uint8_t chunkCountU8 = static_cast<uint8_t>(chunkCount);
        file_.write(reinterpret_cast<const char*>(&packetType), 1);
        file_.write(reinterpret_cast<const char*>(&chunkCountU8), 1);
        file_.write(reinterpret_cast<const char*>(&dataSize), 4);

        // Write chunk sizes
        for (int i = 0; i < chunkCount; i++) {
            file_.write(reinterpret_cast<const char*>(&chunkCompressedSizes[i]), 4);
        }

        // Write chunk data
        for (int i = 0; i < chunkCount; i++) {
            file_.write(chunkBuffers[i].data(), chunkCompressedSizes[i]);
        }

        // Add to I-frame buffer for P-frame references
        IFrameEntry& entry = iFrameBuffer_[iFrameBufferHead_];
        entry.frameNumber = frameCount_;
        entry.pixels = paddedPixels_;
        entry.bc7Data = bc7Buffer_;
        entry.hash = computeHash(paddedPixels_.data(), paddedPixels_.size());

        iFrameBufferHead_ = (iFrameBufferHead_ + 1) % TCV_IFRAME_BUFFER_SIZE;
        if (iFrameBufferCount_ < TCV_IFRAME_BUFFER_SIZE) {
            iFrameBufferCount_++;
        }
        lastIFrameNumber_ = frameCount_;

        statIFrames_++;
    }

    void encodeRefFrame(int refFrameNum) {
        // Write REF-frame packet (just reference, no data)
        uint8_t packetType = TCV_PACKET_REF_FRAME;
        uint32_t refFrame = static_cast<uint32_t>(refFrameNum);

        file_.write(reinterpret_cast<const char*>(&packetType), 1);
        file_.write(reinterpret_cast<const char*>(&refFrame), 4);

        statRefFrames_++;
    }

    void encodePFrame(int refFrameNum, const std::vector<BlockType>& blockTypes) {
        // Step 1: Collect BC7 block indices
        std::vector<int> bc7BlockIndices;
        for (int i = 0; i < totalBlocks_; i++) {
            if (blockTypes[i] == BlockType::BC7) {
                bc7BlockIndices.push_back(i);
            }
        }

        // Step 2: Parallel encode all BC7 blocks
        std::vector<std::array<uint8_t, 256>> bc7Results(bc7BlockIndices.size());

        if (!bc7BlockIndices.empty()) {
            int actualThreads = (numThreads_ > 0) ? numThreads_ : std::thread::hardware_concurrency();
            if (actualThreads < 1) actualThreads = 1;

            bc7enc_compress_block_params params;
            bc7enc_compress_block_params_init(&params);
            switch (quality_) {
                case 0: params.m_max_partitions = 0; params.m_uber_level = 0; break;
                case 1: params.m_max_partitions = 16; params.m_uber_level = 1; break;
                case 2: default: params.m_max_partitions = 64; params.m_uber_level = 4; break;
            }
            if (partitions_ >= 0) params.m_max_partitions = std::clamp(partitions_, 0, 64);
            if (uber_ >= 0) params.m_uber_level = std::clamp(uber_, 0, 4);

            auto encodeBlocks = [&](int startIdx, int endIdx) {
                bc7enc_compress_block_params localParams = params;
                uint8_t block4x4[64];

                for (int idx = startIdx; idx < endIdx; idx++) {
                    int blockIdx = bc7BlockIndices[idx];
                    int bx = blockIdx % blocksX_;
                    int by = blockIdx / blocksX_;
                    int startX = bx * TCV_BLOCK_SIZE;
                    int startY = by * TCV_BLOCK_SIZE;

                    // Encode 16x16 block (4x4 grid of BC7 blocks)
                    for (int by4 = 0; by4 < 4; by4++) {
                        for (int bx4 = 0; bx4 < 4; bx4++) {
                            int x = startX + bx4 * 4;
                            int y = startY + by4 * 4;

                            for (int py = 0; py < 4; py++) {
                                const uint8_t* srcRow = paddedPixels_.data() + ((y + py) * paddedWidth_ + x) * 4;
                                std::memcpy(block4x4 + py * 16, srcRow, 16);
                            }

                            bc7enc_compress_block(bc7Results[idx].data() + (by4 * 4 + bx4) * 16, block4x4, &localParams);
                        }
                    }
                }
            };

            int numBlocks = static_cast<int>(bc7BlockIndices.size());
            if (actualThreads == 1 || numBlocks < actualThreads) {
                encodeBlocks(0, numBlocks);
            } else {
                std::vector<std::thread> threads;
                int blocksPerThread = numBlocks / actualThreads;
                int remainder = numBlocks % actualThreads;
                int current = 0;

                for (int i = 0; i < actualThreads; i++) {
                    int count = blocksPerThread + (i < remainder ? 1 : 0);
                    if (count > 0) {
                        threads.emplace_back(encodeBlocks, current, current + count);
                        current += count;
                    }
                }

                for (auto& t : threads) {
                    t.join();
                }
            }
        }

        // Step 3: Build packet with RLE (sequential)
        framePacketBuffer_.clear();
        int bc7ResultIdx = 0;
        int blockIdx = 0;

        while (blockIdx < totalBlocks_) {
            BlockType type = blockTypes[blockIdx];
            int runLength = 1;

            // Count consecutive blocks of same type (max 128)
            while (blockIdx + runLength < totalBlocks_ &&
                   runLength < 128 &&
                   blockTypes[blockIdx + runLength] == type) {
                runLength++;
            }

            // Write command byte
            uint8_t cmd = (type == BlockType::BC7) ? TCV_BLOCK_BC7 : TCV_BLOCK_SKIP;
            cmd |= (runLength - 1);
            framePacketBuffer_.push_back(cmd);

            // Write BC7 data
            if (type == BlockType::Skip) {
                statSkipBlocks_ += runLength;
            } else {
                for (int i = 0; i < runLength; i++) {
                    statBc7Blocks_++;
                    framePacketBuffer_.insert(framePacketBuffer_.end(),
                                              bc7Results[bc7ResultIdx].begin(),
                                              bc7Results[bc7ResultIdx].end());
                    bc7ResultIdx++;
                }
            }

            blockIdx += runLength;
        }

        // Step 4: Write P-frame packet with LZ4 compression
        uint32_t refFrame = static_cast<uint32_t>(refFrameNum);
        uint32_t uncompressedSize = static_cast<uint32_t>(framePacketBuffer_.size());
        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char*>(framePacketBuffer_.data()),
            lz4Buffer_.data(),
            static_cast<int>(uncompressedSize),
            static_cast<int>(lz4Buffer_.size())
        );

        uint8_t packetType = TCV_PACKET_P_FRAME;
        uint32_t compSize = static_cast<uint32_t>(compressedSize);
        file_.write(reinterpret_cast<const char*>(&packetType), 1);
        file_.write(reinterpret_cast<const char*>(&refFrame), 4);
        file_.write(reinterpret_cast<const char*>(&uncompressedSize), 4);
        file_.write(reinterpret_cast<const char*>(&compSize), 4);
        file_.write(lz4Buffer_.data(), compressedSize);

        statPFrames_++;
    }
};

} // namespace tcx
