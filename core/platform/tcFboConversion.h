#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Private readback conversions. Include after TrussC.h (TextureFormat).
namespace trussc::internal {

// Validate both the row stride and the whole image before allocating staging
// or doing pointer arithmetic. Callers also validate the source GPU resource.
inline bool fboReadbackLayout(int width, int height, size_t pixelBytes,
                              size_t& rowBytes, size_t& totalBytes) {
    if (width <= 0 || height <= 0 || pixelBytes == 0) return false;
    const size_t max = std::numeric_limits<size_t>::max();
    if (size_t(width) > max / pixelBytes) return false;
    rowBytes = size_t(width) * pixelBytes;
    if (size_t(height) > max / rowBytes) return false;
    totalBytes = rowBytes * size_t(height);
    return true;
}

inline float fboHalfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000) << 16;
    int exponent = (h >> 10) & 31;
    uint32_t mantissa = h & 1023;
    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while (!(mantissa & 1024)) { mantissa <<= 1; --exponent; }
            bits = sign | (uint32_t(exponent + 112) << 23) | ((mantissa & 1023) << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000 | (mantissa << 13);
    } else {
        bits = sign | (uint32_t(exponent + 112) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Clamp, then round to nearest with halfway cases rounded up. NaN becomes 0;
// infinities saturate. Desktop GL's native quantisation may differ by one byte
// (notably halfway cases); all CPU conversions use exactly this rule.
inline unsigned char fboUnorm8(float value) {
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<unsigned char>(std::floor(double(value) * 255.0 + 0.5));
}

// Convert one tightly packed source row. R/RG expand as (R,0,0,1)/(R,G,0,1),
// as glReadPixels(GL_RGBA) does. All eleven TextureFormats are covered; there
// is currently no sRGB TextureFormat. BGRA is swizzled, RGBA16 is UNORM.
inline void fboRowToRgba8(const void* source, unsigned char* dst,
                         int width, TextureFormat format) {
    if (format == TextureFormat::RGBA8) {
        std::memcpy(dst, source, size_t(width) * 4);
        return;
    }
    const auto* src = static_cast<const unsigned char*>(source);
    const int channels = channelCount(format);
    const int componentBytes = bytesPerPixel(format) / channels;
    for (int x = 0; x < width; ++x) {
        float rgba[4] = {0, 0, 0, 1};
        for (int c = 0; c < channels; ++c) {
            float value;
            if (isFloatFormat(format)) {
                if (componentBytes == 2) {
                    uint16_t half;
                    std::memcpy(&half, src, sizeof(half));
                    value = fboHalfToFloat(half);
                } else {
                    std::memcpy(&value, src, sizeof(value));
                }
            } else if (componentBytes == 2) {
                uint16_t unorm;
                std::memcpy(&unorm, src, sizeof(unorm));
                value = float(unorm) / 65535.0f;
            } else {
                value = float(*src) / 255.0f;
            }
            src += componentBytes;
            const int out = format == TextureFormat::BGRA8 && c < 3 ? 2 - c : c;
            rgba[out] = value;
        }
        for (int c = 0; c < 4; ++c) *dst++ = fboUnorm8(rgba[c]);
    }
}

} // namespace trussc::internal
