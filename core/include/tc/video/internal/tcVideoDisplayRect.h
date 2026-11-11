#pragma once

#include <cstdint>

namespace trussc::internal {

struct VideoDisplayRect {
    uint32_t x, y, width, height;
};

// Media Foundation offsets use signed 16.16 fixed point. A pixel copy starts
// at the containing pixel when an offset has a fractional component.
inline VideoDisplayRect videoDisplayRect(uint32_t codedWidth, uint32_t codedHeight,
                                        int64_t xFixed, int64_t yFixed,
                                        int64_t width, int64_t height) {
    // Bound (security): an external aperture must fit inside the validated coded frame; otherwise use the whole frame.
    if (width <= 0 || height <= 0 || width > codedWidth || height > codedHeight ||
        xFixed < 0 || yFixed < 0 ||
        xFixed > (int64_t(codedWidth) - width) * 65536 ||
        yFixed > (int64_t(codedHeight) - height) * 65536) {
        return {0, 0, codedWidth, codedHeight};
    }
    return {uint32_t(xFixed / 65536), uint32_t(yFixed / 65536), uint32_t(width), uint32_t(height)};
}

} // namespace trussc::internal
