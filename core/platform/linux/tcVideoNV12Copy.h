#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace trussc::internal {

// Copy admitted NV12 planes, preserving each side's padding. Strides may be
// negative; pointers then address the first displayed row. Callers own storage
// for width Y bytes and 2*ceil(width/2) UV bytes per active row.
inline void copyVideoNV12Planes(uint8_t* const destination[4], const int destinationStrides[4],
                                const uint8_t* const source[4], const int sourceStrides[4],
                                int width, int height) {
    for (int plane = 0; plane < 2; ++plane) {
        const size_t rowBytes = plane == 0 ? size_t(width) : (size_t(width) + 1) / 2 * 2;
        const int rows = plane == 0 ? height : height / 2 + height % 2;
        for (int row = 0; row < rows; ++row) {
            std::memcpy(destination[plane] + ptrdiff_t(row) * destinationStrides[plane],
                        source[plane] + ptrdiff_t(row) * sourceStrides[plane], rowBytes);
        }
    }
}

} // namespace trussc::internal
