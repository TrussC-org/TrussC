#pragma once

#include <cstddef>
#include <cstdint>

namespace trussc::internal {

// RGB32 in a contiguous MF buffer is BGRA. src points to the buffer start;
// negative stride means bottom-up rows, positive stride means top-down rows.
// Zero is treated as tightly packed top-down; setup resolves the actual stride.
inline bool copyGrabberRGB32(unsigned char* dst, const unsigned char* src,
                             std::size_t length, int width, int height,
                             std::int32_t stride) {
    if (!dst || !src || width <= 0 || height <= 0) return false;
    if (static_cast<std::size_t>(width) > length / 4) return false;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    const std::size_t pitch = stride == 0 ? rowBytes : static_cast<std::size_t>(
        stride < 0 ? -static_cast<std::int64_t>(stride) : stride);
    // The last row need not include trailing padding. Division avoids
    // overflowing when checking the span of all rows before any reads.
    if (pitch < rowBytes || static_cast<std::size_t>(height - 1) >
            (length - rowBytes) / pitch) return false;

    for (int y = 0; y < height; ++y) {
        const int sourceY = stride < 0 ? height - 1 - y : y;
        const auto* srcRow = src + static_cast<std::size_t>(sourceY) * pitch;
        auto* dstRow = dst + static_cast<std::size_t>(y) * rowBytes;
        for (std::size_t x = 0; x < rowBytes; x += 4) {
            dstRow[x + 0] = srcRow[x + 2];
            dstRow[x + 1] = srcRow[x + 1];
            dstRow[x + 2] = srcRow[x + 0];
            dstRow[x + 3] = 255;
        }
    }
    return true;
}

} // namespace trussc::internal
