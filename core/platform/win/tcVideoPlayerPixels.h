#pragma once

#include <cstddef>
#include <cstdint>
#include "../../include/tc/utils/tcSimd.h"

namespace trussc::internal {

// Callers validate buffer extents before copying. D3D11 rows are top-down;
// retain alpha and skip staging padding.
inline void copyPlayerStagingBGRA(const unsigned char* src, unsigned char* dst,
                                  int width, int height, std::size_t pitch) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        simd::bgraToRgba(src, dst, static_cast<std::size_t>(width));
        src += pitch;
        dst += rowBytes;
    }
}

// The source-reader buffer starts at the first physical row. Negative stride
// stores bottom-up rows. RGB32 is BGRX here, so replace its undefined alpha.
inline void copyPlayerRGB32(const unsigned char* data, unsigned char* dst,
                            std::uint32_t width, std::uint32_t height,
                            std::int32_t stride) {
    const std::size_t absStride = static_cast<std::size_t>(
        stride < 0 ? -static_cast<std::int64_t>(stride) : stride);
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = stride < 0 ? height - 1 - y : y;
        simd::bgraToRgba(data + static_cast<std::size_t>(sourceY) * absStride,
                         dst, static_cast<std::size_t>(width), true);
        dst += rowBytes;
    }
}

} // namespace trussc::internal
