#pragma once

// =============================================================================
// tcxHapDecoder - HAP Codec Decoder Wrapper
// =============================================================================
// Wraps the Vidvox HAP reference decoder for easy use in TrussC.
// Decodes HAP frames to DXT/BC compressed texture data.

#include <vector>
#include <cstdint>
#include <functional>
#include <thread>

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
    // DXT textures are 4x4 block compressed
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    return blocksX * blocksY * getBytesPerBlock(format);
}

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

    // Get last chunk count (for debugging, -1 = callback not called)
    int getLastChunkCount() const { return lastChunkCount_; }

    // Decode a HAP frame
    // Input: raw HAP frame data from MOV container
    // Output: DXT/BC compressed texture data
    bool decode(const uint8_t* frameData, size_t frameSize,
                uint32_t width, uint32_t height,
                HapDecodedFrame& result) {

        if (!frameData || frameSize == 0) {
            return false;
        }

        // Get texture count (HAP can have multiple textures per frame)
        unsigned int textureCount = 0;
        unsigned int hapResult = HapGetFrameTextureCount(frameData, frameSize, &textureCount);
        if (hapResult != HapResult_No_Error || textureCount == 0) {
            return false;
        }

        // Get format of first texture
        unsigned int hapFormat = 0;
        hapResult = HapGetFrameTextureFormat(frameData, frameSize, 0, &hapFormat);
        if (hapResult != HapResult_No_Error) {
            return false;
        }

        result.format = hapTextureFormatToEnum(hapFormat);
        if (result.format == HapFormat::Unknown) {
            return false;
        }

        result.width = width;
        result.height = height;

        // Calculate output buffer size
        size_t outputSize = calculateTextureSize(width, height, result.format);
        result.data.resize(outputSize);

        // Decode
        unsigned long bytesUsed = 0;
        unsigned int outputFormat = 0;

        hapResult = HapDecode(
            frameData, frameSize,
            0,  // texture index
            hapDecodeCallback, this,
            result.data.data(), outputSize,
            &bytesUsed, &outputFormat
        );

        if (hapResult != HapResult_No_Error) {
            result.data.clear();
            return false;
        }

        return true;
    }

    // Decode with pre-allocated buffer (for performance)
    bool decodeToBuffer(const uint8_t* frameData, size_t frameSize,
                        uint32_t width, uint32_t height,
                        uint8_t* outputBuffer, size_t outputBufferSize,
                        HapFormat& outFormat) {

        if (!frameData || frameSize == 0 || !outputBuffer) {
            return false;
        }

        // Get format
        unsigned int hapFormat = 0;
        unsigned int hapResult = HapGetFrameTextureFormat(frameData, frameSize, 0, &hapFormat);
        if (hapResult != HapResult_No_Error) {
            return false;
        }

        outFormat = hapTextureFormatToEnum(hapFormat);
        if (outFormat == HapFormat::Unknown) {
            return false;
        }

        // Verify buffer size
        size_t requiredSize = calculateTextureSize(width, height, outFormat);
        if (outputBufferSize < requiredSize) {
            return false;
        }

        // Decode
        unsigned long bytesUsed = 0;
        unsigned int outputFormatOut = 0;

        hapResult = HapDecode(
            frameData, frameSize,
            0,
            hapDecodeCallback, this,
            outputBuffer, outputBufferSize,
            &bytesUsed, &outputFormatOut
        );

        return hapResult == HapResult_No_Error;
    }

private:
    mutable int lastChunkCount_ = -1;  // -1 = callback never called

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

        // Parallel decode using threads
        std::vector<std::thread> threads;
        threads.reserve(count);

        for (unsigned int i = 0; i < count; i++) {
            threads.emplace_back([work, p, i]() {
                work(p, i);
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }
};

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------

// Check if data looks like a HAP frame
inline bool isHapFrame(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;

    unsigned int textureCount = 0;
    unsigned int result = HapGetFrameTextureCount(data, size, &textureCount);
    return result == HapResult_No_Error && textureCount > 0;
}

// Get HAP format from frame data
inline HapFormat getHapFrameFormat(const uint8_t* data, size_t size) {
    if (!data || size < 4) return HapFormat::Unknown;

    unsigned int hapFormat = 0;
    unsigned int result = HapGetFrameTextureFormat(data, size, 0, &hapFormat);
    if (result != HapResult_No_Error) return HapFormat::Unknown;

    return hapTextureFormatToEnum(hapFormat);
}

} // namespace tcx::hap
