#pragma once

// =============================================================================
// tcxHapDecoder - HAP Codec Decoder Wrapper
// =============================================================================
// Wraps the Vidvox HAP reference decoder for easy use in TrussC.
// Decodes HAP frames to DXT/BC compressed texture data.

#include <TrussC.h>
#ifdef _WIN32
TC_NO_HOT_RELOAD("tcxHap: snappy is not linked into the hot-reload guest on Windows");
#endif

#include <vector>
#include <cstdint>
#include <functional>
#include <thread>
#include <array>
#include <atomic>
#include <limits>
#include <tc/utils/tcLog.h>
#include <utility>
#include "impl/tcxMovIndex.h"

// HAP reference decoder (BSD-2-Clause)
extern "C" {
#include "hap.h"
}

namespace tcx::hap {

// -----------------------------------------------------------------------------
// HAP texture format mapping to sokol BC formats
// -----------------------------------------------------------------------------
enum class HapFormat {
    Unknown = 0,
    DXT1,       // HAP - BC1
    DXT5,       // HAP Alpha - BC3
    YCoCgDXT5,  // HAPQ - needs shader conversion
    BC7,        // HAP R - BC7
    RGTC1       // HAP Alpha Only - BC4
};

// Convert HAP texture format to our enum
inline HapFormat hapTextureFormatToEnum(unsigned int hapFormat) {
    switch (hapFormat) {
        case HapTextureFormat_RGB_DXT1:
            return HapFormat::DXT1;
        case HapTextureFormat_RGBA_DXT5:
            return HapFormat::DXT5;
        case HapTextureFormat_YCoCg_DXT5:
            return HapFormat::YCoCgDXT5;
        case HapTextureFormat_RGBA_BPTC_UNORM:
            return HapFormat::BC7;
        case HapTextureFormat_A_RGTC1:
            return HapFormat::RGTC1;
        default:
            return HapFormat::Unknown;
    }
}

// Get bytes per block for format
inline size_t getBytesPerBlock(HapFormat format) {
    switch (format) {
        case HapFormat::DXT1:
        case HapFormat::RGTC1:
            return 8;  // BC1, BC4: 8 bytes per 4x4 block
        case HapFormat::DXT5:
        case HapFormat::YCoCgDXT5:
        case HapFormat::BC7:
            return 16; // BC3, BC7: 16 bytes per 4x4 block
        default:
            return 0;
    }
}

// Calculate texture data size
inline size_t calculateTextureSize(uint32_t width, uint32_t height, HapFormat format) {
    if (!detail::movDimensions(width, height)) return 0;
    // DXT textures are 4x4 block compressed
    const size_t blockSize = getBytesPerBlock(format);
    const uint64_t blocksX = (uint64_t(width) + 3) / 4;
    const uint64_t blocksY = (uint64_t(height) + 3) / 4;
    // Bound (abi): Texture bytes must fit size_t and HAP unsigned long; Overflow rejects the frame and is counted.
    const size_t limit = std::numeric_limits<size_t>::max() < std::numeric_limits<unsigned long>::max() ?
                         std::numeric_limits<size_t>::max() : std::numeric_limits<unsigned long>::max();
    if (blockSize == 0 || blocksX == 0 || blocksY == 0 || blocksX > limit / blockSize / blocksY) return 0;
    return size_t(blocksX * blocksY * blockSize);
}

} // namespace tcx::hap

#include "impl/tcxHapFrameValidation.h"

namespace tcx::hap {

// -----------------------------------------------------------------------------
// Decoded frame result
// -----------------------------------------------------------------------------
struct HapDecodedFrame {
    std::vector<uint8_t> data;
    HapFormat format = HapFormat::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;

    bool isValid() const {
        return format != HapFormat::Unknown && !data.empty();
    }
};

// -----------------------------------------------------------------------------
// HAP Decoder
// -----------------------------------------------------------------------------
class HapDecoder {
public:
    HapDecoder() = default;

    // Get last decoded chunk count (for debugging, -1 = no count available)
    int getLastChunkCount() const { return lastChunkCount_; }
    uint64_t getInvalidFrameCount() const { return invalidFrameCount_; }

    // Decode a HAP frame
    // Input: raw HAP frame data from MOV container
    // Output: DXT/BC compressed texture data
    bool decode(const uint8_t* frameData, size_t frameSize,
                uint32_t width, uint32_t height,
                HapDecodedFrame& result) {

        result = {};
        lastChunkCount_ = -1;
        lastErrorCode_ = HapResult_Bad_Frame;
        if (!detail::movDimensions(width, height) ||
            !detail::validateHapFrame(frameData, frameSize, width, height, result.format)) {
            result = {};
            return invalidFrame(width, height, frameSize);
        }

        result.width = width;
        result.height = height;

        // Calculate output buffer size
        size_t outputSize = calculateTextureSize(width, height, result.format);
        if (!detail::resizeMovBuffer(result.data, outputSize)) {
            trussc::logError("HapDecoder") << "HAP output allocation failed";
            result = {};
            return false;
        }

        // Decode
        unsigned long bytesUsed = 0;
        unsigned int outputFormat = 0;

        unsigned int hapResult = HapDecode(
            frameData, frameSize,
            0,  // texture index
            hapDecodeCallback, this,
            result.data.data(), outputSize,
            &bytesUsed, &outputFormat
        );

        lastErrorCode_ = hapResult == HapResult_No_Error && bytesUsed != outputSize ?
                         HapResult_Bad_Frame : hapResult;
        if (hapResult != HapResult_No_Error || bytesUsed != outputSize) {
            result = {};
            return lastErrorCode_ == HapResult_Bad_Frame ? invalidFrame(width, height, frameSize) : false;
        }

        // HAP decodes simple single-chunk frames without invoking the callback.
        if (lastChunkCount_ < 0) lastChunkCount_ = 1;
        return true;
    }

    // Decode with pre-allocated buffer (for performance)
    bool decodeToBuffer(const uint8_t* frameData, size_t frameSize,
                        uint32_t width, uint32_t height,
                        uint8_t* outputBuffer, size_t outputBufferSize,
                        HapFormat& outFormat) {

        lastChunkCount_ = -1;
        lastErrorCode_ = HapResult_Bad_Arguments;
        outFormat = HapFormat::Unknown;
        if (!outputBuffer) return false;
        if (!detail::movDimensions(width, height) ||
            !detail::validateHapFrame(frameData, frameSize, width, height, outFormat)) {
            outFormat = HapFormat::Unknown;
            return invalidFrame(width, height, frameSize);
        }

        // Verify buffer size
        size_t requiredSize = calculateTextureSize(width, height, outFormat);
        if (outputBufferSize < requiredSize) {
            lastErrorCode_ = HapResult_Buffer_Too_Small;
            return false;
        }

        // Decode
        unsigned long bytesUsed = 0;
        unsigned int outputFormatOut = 0;

        unsigned int hapResult = HapDecode(
            frameData, frameSize,
            0,
            hapDecodeCallback, this,
            outputBuffer, requiredSize,
            &bytesUsed, &outputFormatOut
        );

        lastErrorCode_ = hapResult == HapResult_No_Error && bytesUsed != requiredSize ?
                         HapResult_Bad_Frame : hapResult;
        if (lastErrorCode_ != HapResult_No_Error) {
            outFormat = HapFormat::Unknown;
            return lastErrorCode_ == HapResult_Bad_Frame ? invalidFrame(width, height, frameSize) : false;
        }
        // HAP decodes simple single-chunk frames without invoking the callback.
        if (lastChunkCount_ < 0) lastChunkCount_ = 1;
        return true;
    }

private:
    friend class HapPlayer;
    unsigned int lastErrorCode_ = 0;
    mutable int lastChunkCount_ = -1;  // -1 = no count available
    // Copies preserve the diagnostic state; later warnings are independent.
    bool invalidFrameWarned_ = false;
    uint64_t invalidFrameCount_ = 0;

    bool invalidFrame(uint32_t width, uint32_t height, size_t frameSize) {
        lastErrorCode_ = HapResult_Bad_Frame;
        ++invalidFrameCount_;
        if (!std::exchange(invalidFrameWarned_, true)) {
            trussc::logWarning("HapDecoder") << "Skipping invalid HAP frame (needed " << width << "x" << height << ", " << frameSize
                << " encoded bytes); invalid frames will be skipped";
        }
        return false;
    }

    // HAP decode callback for parallel decoding
    static void hapDecodeCallback(HapDecodeWorkFunction work, void* p, unsigned int count, void* info) {
        // Store chunk count (always, even for count=0 or 1)
        if (info) {
            auto* decoder = static_cast<HapDecoder*>(info);
            decoder->lastChunkCount_ = static_cast<int>(count);
        }

        if (count <= 1) {
            // Single-threaded
            for (unsigned int i = 0; i < count; i++) {
                work(p, i);
            }
            return;
        }

        // Bound (security): file chunk counts cannot determine OS thread usage. Overflow uses bounded workers without losing chunks.
        constexpr unsigned int maxWorkers = 8;
        std::array<std::thread, maxWorkers> threads;
        std::atomic<unsigned int> nextChunk{0};
        auto decodeChunks = [&]() {
            for (;;) {
                const unsigned int i = nextChunk.fetch_add(1, std::memory_order_relaxed);
                if (i >= count) return;
                work(p, i);
            }
        };
        const unsigned int workerCount = count < maxWorkers ? count : maxWorkers;
        try {
            for (unsigned int i = 0; i < workerCount; ++i) {
                threads[i] = std::thread(decodeChunks);
            }
        } catch (...) {
            // A failed thread construction must not unwind past joinable workers.
            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }
            decodeChunks();
            return;
        }

        // Workers and their captured callback data never outlive this call.
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }
};

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------

// Check if data looks like a HAP frame
inline bool isHapFrame(const uint8_t* data, size_t size) {
    HapFormat format = HapFormat::Unknown;
    return detail::validateHapFrame(data, size, 0, 0, format, false);
}

// Get HAP format from frame data
inline HapFormat getHapFrameFormat(const uint8_t* data, size_t size) {
    // Discovery only reads the texture header; decode validates its tables/data.
    detail::HapSection texture;
    if (!data || !detail::readHapSection(data, size, texture)) return HapFormat::Unknown;
    if (texture.type == 0xd) {
        detail::HapSection first;
        if (!detail::readHapSection(texture.data, texture.size, first)) return HapFormat::Unknown;
        texture = first;
    }
    return detail::hapSectionFormat(texture.type);
}

} // namespace tcx::hap
