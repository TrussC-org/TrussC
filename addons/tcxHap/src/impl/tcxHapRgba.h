#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tcx::hap::detail {

// Decode RGBA BC blocks, clipping partial blocks at the frame edges.
inline void decodeRgbaBlocks(const uint8_t* src, uint8_t* dst, int width, int height,
                             size_t blockSize,
                             void (*decodeBlock)(const void*, void*, int)) {
    const size_t pitch = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            uint8_t* blockDst = dst + static_cast<size_t>(y) * pitch + x * 4;
            const int rows = std::min(4, height - y);
            const int columns = std::min(4, width - x);
            if (rows == 4 && columns == 4) {
                decodeBlock(src, blockDst, static_cast<int>(pitch));
            } else {
                alignas(uint32_t) uint8_t block[64];
                decodeBlock(src, block, 16);
                for (int row = 0; row < rows; ++row) {
                    std::memcpy(blockDst + row * pitch, block + row * 16, columns * 4);
                }
            }
            src += blockSize;
        }
    }
}

} // namespace tcx::hap::detail
