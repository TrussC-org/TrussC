#pragma once

#include <cstddef>
#include <limits>

namespace trussc::internal {

enum class RecorderPixelFormat { Unknown, RGBA8, BGRA8, RGB10A2 };

struct RecorderFrameLayout {
    size_t width = 0;
    size_t height = 0;
    size_t rowBytes = 0;
    size_t byteSize = 0;
    RecorderPixelFormat format = RecorderPixelFormat::Unknown;
};

inline bool recorderByteSize(size_t rowBytes, size_t height, size_t& bytes) {
    // Bound (abi): row extents must fit size_t. Overflow drops the frame and is counted.
    if (!rowBytes || !height || rowBytes > std::numeric_limits<size_t>::max() / height)
        return false;
    bytes = rowBytes * height;
    return true;
}

inline RecorderFrameLayout recorderPackedLayout(size_t width, size_t height,
                                                RecorderPixelFormat format) {
    RecorderFrameLayout layout{width, height, 0, 0, format};
    // Bound (abi): four-byte pixels must fit the native row size. Overflow drops the frame and is counted.
    if (width <= std::numeric_limits<size_t>::max() / 4) {
        layout.rowBytes = width * 4;
        recorderByteSize(layout.rowBytes, height, layout.byteSize);
    }
    return layout;
}

inline bool recorderFrameFits(const RecorderFrameLayout& layout, size_t width,
                              size_t height, RecorderPixelFormat format) {
    const auto packed = recorderPackedLayout(width, height, format);
    size_t bytes = 0;
    // Bound (abi): the take must fit the source or native buffer layout and capacity. Overflow drops the frame and is counted.
    return format != RecorderPixelFormat::Unknown && packed.byteSize && layout.width == width && layout.height == height &&
        layout.format == format && layout.rowBytes >= packed.rowBytes &&
        recorderByteSize(layout.rowBytes, height, bytes) && layout.byteSize >= bytes;
}

inline bool recorderNV12ByteSize(size_t stride, size_t height, size_t& bytes) {
    size_t luma = 0;
    // Bound (abi): even NV12 planes and their sum must fit size_t. Overflow drops the frame and is counted.
    if ((stride & 1) || (height & 1) || !recorderByteSize(stride, height, luma) ||
        luma > std::numeric_limits<size_t>::max() - luma / 2) return false;
    bytes = luma + luma / 2;
    return true;
}

// Active bytes through the final UV pixel; final-row padding is optional.
inline bool recorderNV12ImageSpan(size_t width, size_t height, size_t stride,
                                  size_t sliceHeight, size_t& bytes) {
    size_t allocation = 0;
    // Bound (abi): active NV12 rows must fit the padded plane allocation. Overflow drops the frame and is counted.
    if (!width || !height || (width & 1) || (height & 1) ||
        width > stride || height > sliceHeight ||
        !recorderNV12ByteSize(stride, sliceHeight, allocation)) return false;
    // The checked full allocation bounds both products and their sum.
    bytes = stride * sliceHeight + stride * (height / 2 - 1) + width;
    return true;
}

inline bool recorderNV12ImageFits(size_t width, size_t height, size_t stride,
                                  size_t sliceHeight, size_t capacity) {
    size_t bytes = 0;
    // Bound (abi): native capacity must include every active Y and UV pixel. Overflow drops the frame and is counted.
    return recorderNV12ImageSpan(width, height, stride, sliceHeight, bytes) && bytes <= capacity;
}

struct RecorderFrameBuffer {
    unsigned char* data = nullptr;
    RecorderFrameLayout layout;
};

} // namespace trussc::internal
