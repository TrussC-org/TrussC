#pragma once

#include <cstddef>
#include <cstdint>
#include "../../include/tc/video/internal/tcVideoDisplayRect.h"
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
                            std::uint32_t /*codedWidth*/, std::uint32_t codedHeight,
                            std::int32_t stride, const VideoDisplayRect& display) {
    const std::size_t absStride = static_cast<std::size_t>(
        stride < 0 ? -static_cast<std::int64_t>(stride) : stride);
    // Bound (security): callers validate the display rectangle inside the coded frame and both buffer extents.
    const std::size_t rowBytes = static_cast<std::size_t>(display.width) * 4;
    for (std::uint32_t y = 0; y < display.height; ++y) {
        const std::uint32_t frameRow = display.y + y;
        const std::uint32_t sourceY = stride < 0 ? codedHeight - 1 - frameRow : frameRow;
        simd::bgraToRgba(data + static_cast<std::size_t>(sourceY) * absStride +
                            static_cast<std::size_t>(display.x) * 4,
                         dst, static_cast<std::size_t>(display.width), true);
        dst += rowBytes;
    }
}

} // namespace trussc::internal
