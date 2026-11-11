#pragma once

// Original row loops copied verbatim from origin/main for #1361 part 2.
// Indentation is normalized; wrapper variables/byte aliases replace platform
// objects. Loop statements and comments are unchanged; no OS SDK is needed.
namespace originalPixels {
using BYTE = unsigned char;
using UINT32 = std::uint32_t;

inline void macGrabber(unsigned char* src, unsigned char* dst, size_t width, size_t height, size_t bytesPerRow) {
    for (size_t y = 0; y < height; y++) {
        unsigned char* srcRow = src + y * bytesPerRow;
        unsigned char* dstRow = dst + y * width * 4;
        for (size_t x = 0; x < width; x++) {
            size_t srcIdx = x * 4;
            size_t dstIdx = x * 4;
            dstRow[dstIdx + 0] = srcRow[srcIdx + 2];  // R <- B
            dstRow[dstIdx + 1] = srcRow[srcIdx + 1];  // G <- G
            dstRow[dstIdx + 2] = srcRow[srcIdx + 0];  // B <- R
            dstRow[dstIdx + 3] = 255;                  // A = 不透明
        }
    }
}

inline void macPlayer(unsigned char* baseAddress, unsigned char* _pixelBuffer, size_t copyW, size_t copyH, size_t _videoWidth, size_t bytesPerRow) {
    for (size_t y = 0; y < copyH; y++) {
        unsigned char* srcRow = baseAddress + y * bytesPerRow;
        unsigned char* dstRow = _pixelBuffer + y * (size_t)_videoWidth * 4;

        for (size_t x = 0; x < copyW; x++) {
            // BGRA -> RGBA
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G <- G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A <- A
        }
    }
}

inline void macRecorder(const unsigned char* rgba, uint8_t* dst, int w, int h, size_t dstStride) {
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = rgba + (size_t)y * w * 4;
        uint8_t* drow = dst + (size_t)y * dstStride;
        for (size_t x = 0; x < (size_t)w; ++x) {
            drow[x * 4 + 0] = srow[x * 4 + 2];  // B
            drow[x * 4 + 1] = srow[x * 4 + 1];  // G
            drow[x * 4 + 2] = srow[x * 4 + 0];  // R
            drow[x * 4 + 3] = srow[x * 4 + 3];  // A
        }
    }
}

inline void iosGrabber(unsigned char* src, unsigned char* dst, size_t width, size_t height, size_t bytesPerRow) {
    for (size_t y = 0; y < height; y++) {
        unsigned char* srcRow = src + y * bytesPerRow;
        unsigned char* dstRow = dst + y * width * 4;
        for (size_t x = 0; x < width; x++) {
            size_t srcIdx = x * 4;
            size_t dstIdx = x * 4;
            dstRow[dstIdx + 0] = srcRow[srcIdx + 2];  // R <- B
            dstRow[dstIdx + 1] = srcRow[srcIdx + 1];  // G <- G
            dstRow[dstIdx + 2] = srcRow[srcIdx + 0];  // B <- R
            dstRow[dstIdx + 3] = 255;                  // A = opaque
        }
    }
}

inline void iosPlayer(unsigned char* baseAddress, unsigned char* _pixelBuffer, size_t copyW, size_t copyH, size_t _videoWidth, size_t bytesPerRow) {
    for (size_t y = 0; y < copyH; y++) {
        unsigned char* srcRow = baseAddress + y * bytesPerRow;
        unsigned char* dstRow = _pixelBuffer + y * size_t(_videoWidth) * 4;

        for (size_t x = 0; x < copyW; x++) {
            // BGRA -> RGBA
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G <- G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A <- A
        }
    }
}

inline void iosRecorder(const unsigned char* rgba, uint8_t* dst, int w, int h, size_t dstStride) {
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = rgba + (size_t)y * w * 4;
        uint8_t* drow = dst + (size_t)y * dstStride;
        for (size_t x = 0; x < (size_t)w; ++x) {
            drow[x * 4 + 0] = srow[x * 4 + 2];  // B
            drow[x * 4 + 1] = srow[x * 4 + 1];  // G
            drow[x * 4 + 2] = srow[x * 4 + 0];  // R
            drow[x * 4 + 3] = srow[x * 4 + 3];  // A
        }
    }
}

inline void winStaging(unsigned char* src, unsigned char* dst, int width, int height, size_t pitch) {
    struct State { int width_, height_; } state{width, height};
    auto* state_ = &state;
    struct { size_t RowPitch; } mapped{pitch};
    int rowBytes = width * 4;
    for (int y = 0; y < state_->height_; y++) {
        // BGRA to RGBA conversion
        for (int x = 0; x < state_->width_; x++) {
            int srcIdx = x * 4;
            int dstIdx = x * 4;
            dst[dstIdx + 0] = src[srcIdx + 2]; // R <- B
            dst[dstIdx + 1] = src[srcIdx + 1]; // G <- G
            dst[dstIdx + 2] = src[srcIdx + 0]; // B <- R
            dst[dstIdx + 3] = src[srcIdx + 3]; // A <- A
        }
        src += mapped.RowPitch;
        dst += rowBytes;
    }
}

inline void winRGB32(const BYTE* data, unsigned char* dst, UINT32 width, UINT32 height, std::int32_t stride) {
    const size_t absStride = size_t(stride < 0 ? -int64_t(stride) : int64_t(stride));
    int rowBytes = (int)width * 4;
    for (UINT32 y = 0; y < height; y++) {
        // Negative stride means the image is stored bottom-up.
        UINT32 srcRow = stride < 0 ? (height - 1 - y) : y;
        const BYTE* src = data + (size_t)srcRow * absStride;
        // BGRA to RGBA conversion
        for (UINT32 x = 0; x < width; x++) {
            int srcIdx = x * 4;
            int dstIdx = x * 4;
            dst[dstIdx + 0] = src[srcIdx + 2]; // R <- B
            dst[dstIdx + 1] = src[srcIdx + 1]; // G <- G
            dst[dstIdx + 2] = src[srcIdx + 0]; // B <- R
            // RGB32 from the source reader is BGRX: the 4th byte
            // is undefined padding, not alpha. Force opaque so the
            // extracted frame isn't fully transparent.
            dst[dstIdx + 3] = 255;             // A = opaque
        }
        dst += rowBytes;
    }
}
} // namespace originalPixels
