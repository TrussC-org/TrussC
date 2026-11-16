#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "tc/gpu/tcTexture.h"

namespace trussc::internal {

inline TextureFormat iblEquirectFormat(bool float32Filterable) {
    return float32Filterable ? TextureFormat::RGBA32F : TextureFormat::RGBA16F;
}

// Round to IEEE binary16, ties to even. Clamp to the half-float finite range
// (+/-65504), preserving HDR values above 1 without overflowing to infinity.
inline uint16_t iblFloatToHalf(float value) {
    value = std::clamp(value, -65504.0f, 65504.0f);
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint16_t sign = uint16_t((bits >> 16) & 0x8000);
    bits &= 0x7fffffff;
    if (bits > 0x7f800000) return uint16_t(sign | 0x7e00); // quiet NaN
    if (bits >= 0x38800000) {
        bits += 0xfff + ((bits >> 13) & 1);
        return uint16_t(sign | ((bits - 0x38000000) >> 13));
    }
    const uint32_t exponent = bits >> 23;
    if (exponent < 102) return sign;
    const uint32_t significand = (bits & 0x7fffff) | 0x800000;
    const uint32_t shift = 126 - exponent;
    uint32_t rounded = significand >> shift;
    const uint32_t remainder = significand & ((uint32_t(1) << shift) - 1);
    const uint32_t midpoint = uint32_t(1) << (shift - 1);
    if (remainder > midpoint || (remainder == midpoint && (rounded & 1))) ++rounded;
    return uint16_t(sign | rounded);
}

} // namespace trussc::internal
