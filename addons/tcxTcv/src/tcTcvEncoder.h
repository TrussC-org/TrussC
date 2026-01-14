#pragma once

// =============================================================================
// tcTcvEncoder.h - TCVC video encoder
// =============================================================================

#include <TrussC.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <thread>
#include <atomic>

// BC7 encoder
#include "impl/bc7enc.h"

namespace tcx {
// TCVC File Format Constants
// ---------------------------------------------------------------------------
constexpr uint32_t TCV_SIGNATURE = 0x56435654;  // "TCVC" little-endian
constexpr uint16_t TCV_VERSION = 1;
constexpr uint16_t TCV_HEADER_SIZE = 64;
constexpr uint16_t TCV_BLOCK_SIZE = 16;  // 16x16 pixel blocks

// Packet types
constexpr uint8_t TCV_PACKET_REF_FRAME = 0x01;
constexpr uint8_t TCV_PACKET_NEW_FRAME = 0x02;

// ---------------------------------------------------------------------------
// TCVC Header Structure (64 bytes)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct TcvHeader {
    char     signature[4];    // 0x00: "TCVC"
    uint16_t version;         // 0x04: 1
    uint16_t headerSize;      // 0x06: 64
    uint32_t width;           // 0x08
    uint32_t height;          // 0x0C
    uint32_t frameCount;      // 0x10
    float    fps;             // 0x14
    uint16_t blockSize;       // 0x18: 16
    uint16_t reserved1;       // 0x1A
    uint32_t reserved2;       // 0x1C
    uint32_t reserved3;       // 0x20
    uint32_t reserved4;       // 0x24
    uint32_t audioCodec;      // 0x28: FourCC (0=none)
    uint32_t reserved5;       // 0x2C
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

        // Validate dimensions (must be divisible by block size for simplicity)
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

        // Write placeholder header (will update frameCount at end)
        TcvHeader header = {};
        std::memcpy(header.signature, "TCVC", 4);
        header.version = TCV_VERSION;
        header.headerSize = TCV_HEADER_SIZE;
        header.width = width;
        header.height = height;
        header.frameCount = 0;  // Will be updated in end()
        header.fps = fps;
        header.blockSize = TCV_BLOCK_SIZE;
        header.audioCodec = 0;
        header.audioOffset = 0;
        header.audioSize = 0;

        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // Allocate padded pixel buffer
        paddedWidth_ = blocksX_ * TCV_BLOCK_SIZE;
        paddedHeight_ = blocksY_ * TCV_BLOCK_SIZE;
        paddedPixels_.resize(paddedWidth_ * paddedHeight_ * 4, 0);

        // Allocate BC7 output buffer (one frame worth of blocks)
        // Each 16x16 block = 16 BC7 4x4 blocks, each BC7 block = 16 bytes
        bc7Buffer_.resize(blocksX_ * blocksY_ * 16 * 16);

        isEncoding_ = true;
        
        int actualThreads = (numThreads_ > 0) ? numThreads_ : std::thread::hardware_concurrency();
        if (actualThreads < 1) actualThreads = 1;

        tc::logNotice("TcvEncoder") << "Started encoding: " << width << "x" << height
                                    << " @ " << fps << " fps (" << actualThreads << " threads)";
        return true;
    }

    bool addFrame(const unsigned char* rgbaPixels) {
        if (!isEncoding_) {
            tc::logError("TcvEncoder") << "Not encoding";
            return false;
        }

        // Copy pixels to padded buffer (handles non-block-aligned sizes)
        copyToPadded(rgbaPixels);

        // Encode all blocks to BC7
        encodeAllBlocks();

        // Write frame packet
        uint8_t packetType = TCV_PACKET_NEW_FRAME;
        uint32_t dataSize = static_cast<uint32_t>(bc7Buffer_.size());

        file_.write(reinterpret_cast<const char*>(&packetType), 1);
        file_.write(reinterpret_cast<const char*>(&dataSize), 4);
        file_.write(reinterpret_cast<const char*>(bc7Buffer_.data()), dataSize);

        frameCount_++;
        return true;
    }

    bool end() {
        if (!isEncoding_) {
            return false;
        }

        // Update header with actual frame count
        file_.seekp(0x10);
        file_.write(reinterpret_cast<const char*>(&frameCount_), 4);

        file_.close();
        isEncoding_ = false;

        tc::logNotice("TcvEncoder") << "Finished encoding: " << frameCount_ << " frames";
        return true;
    }

    // =========================================================================
    // State
    // =========================================================================

    bool isEncoding() const { return isEncoding_; }
    uint32_t getFrameCount() const { return frameCount_; }

    // Quality: 0=fast, 1=balanced, 2=high
    void setQuality(int quality) { quality_ = std::clamp(quality, 0, 2); }

    // Manual BC7 parameters (overrides quality preset if >= 0)
    void setPartitions(int partitions) { partitions_ = partitions; }
    void setUberLevel(int uber) { uber_ = uber; }

    // Thread count (0 = auto/max cores)
    void setThreadCount(int numThreads) {
        numThreads_ = numThreads;
        if (numThreads_ < 0) numThreads_ = 0; // 0 means auto
    }

private:
    std::ofstream file_;
    bool isEncoding_ = false;
    int quality_ = 1;  // 0=fast, 1=balanced, 2=high
    int partitions_ = -1;  // Manual override (-1 = use quality preset)
    int uber_ = -1;        // Manual override (-1 = use quality preset)
    int numThreads_ = 0;   // 0 = auto

    int width_ = 0;
    int height_ = 0;
    float fps_ = 0;
    uint32_t frameCount_ = 0;

    int blocksX_ = 0;
    int blocksY_ = 0;
    int paddedWidth_ = 0;
    int paddedHeight_ = 0;

    std::vector<uint8_t> paddedPixels_;
    std::vector<uint8_t> bc7Buffer_;

    bc7enc_compress_block_params bc7Params_;

    // Copy source pixels to padded buffer
    void copyToPadded(const unsigned char* src) {
        // Clear to black (for padding)
        std::fill(paddedPixels_.begin(), paddedPixels_.end(), 0);

        // Copy row by row
        for (int y = 0; y < height_; y++) {
            const unsigned char* srcRow = src + y * width_ * 4;
            unsigned char* dstRow = paddedPixels_.data() + y * paddedWidth_ * 4;
            std::memcpy(dstRow, srcRow, width_ * 4);
        }
    }

    // Encode all BC7 4x4 blocks in row-major order (GPU expected layout)
    void encodeAllBlocks() {
        bc7enc_compress_block_params_init(&bc7Params_);

        // Quality settings: 0=fast, 1=balanced, 2=high
        switch (quality_) {
            case 0:  // fast
                bc7Params_.m_max_partitions = 0;
                bc7Params_.m_uber_level = 0;
                break;
            case 1:  // balanced
                bc7Params_.m_max_partitions = 16;
                bc7Params_.m_uber_level = 1;
                break;
            case 2:  // high
            default:
                bc7Params_.m_max_partitions = 64;
                bc7Params_.m_uber_level = 4;
                break;
        }

        // Manual overrides
        if (partitions_ >= 0) {
            bc7Params_.m_max_partitions = std::clamp(partitions_, 0, 64);
        }
        if (uber_ >= 0) {
            bc7Params_.m_uber_level = std::clamp(uber_, 0, 4);
        }

        // Determine thread count
        int actualThreads = (numThreads_ > 0) ? numThreads_ : std::thread::hardware_concurrency();
        if (actualThreads < 1) actualThreads = 1;

        // Number of BC7 4x4 blocks
        int bc7BlocksX = paddedWidth_ / 4;
        int bc7BlocksY = paddedHeight_ / 4;

        // Function to process a range of block rows
        auto processRows = [&](int startBy, int endBy) {
            // Local params copy for thread safety
            bc7enc_compress_block_params localParams = bc7Params_;
            uint8_t block4x4[64]; // 4*4*4 bytes

            for (int by = startBy; by < endBy; by++) {
                // Output pointer for this row
                uint8_t* outPtr = bc7Buffer_.data() + (by * bc7BlocksX * 16);

                for (int bx = 0; bx < bc7BlocksX; bx++) {
                    int startX = bx * 4;
                    int startY = by * 4;

                    // Extract 4x4 pixels
                    for (int py = 0; py < 4; py++) {
                        int srcY = startY + py;
                        const uint8_t* srcRow = paddedPixels_.data() + srcY * paddedWidth_ * 4 + startX * 4;
                        std::memcpy(block4x4 + py * 16, srcRow, 16);
                    }

                    // Encode BC7 block
                    bc7enc_compress_block(outPtr, block4x4, &localParams);
                    outPtr += 16;
                }
            }
        };

        if (actualThreads == 1) {
            processRows(0, bc7BlocksY);
        } else {
            std::vector<std::thread> threads;
            int rowsPerThread = bc7BlocksY / actualThreads;
            int remainingRows = bc7BlocksY % actualThreads;
            int currentBy = 0;

            for (int i = 0; i < actualThreads; i++) {
                int count = rowsPerThread + (i < remainingRows ? 1 : 0);
                if (count > 0) {
                    threads.emplace_back(processRows, currentBy, currentBy + count);
                    currentBy += count;
                }
            }

            for (auto& t : threads) {
                t.join();
            }
        }
    }
};

} // namespace tcx
