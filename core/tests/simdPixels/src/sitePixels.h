#pragma once

struct SiteLayout {
    int width, height, outputWidth;
    size_t pitch;
    trussc::internal::VideoDisplayRect display{0, 0, std::uint32_t(width), std::uint32_t(height)};
};
using RowKernel = void (*)(std::uint8_t*, std::uint8_t*, const SiteLayout&);
struct RowSite {
    const char* name;
    RowKernel before, after;
    bool packedInput = false;
};

// Portable copies of the converted Apple row loops, with their original row
// expressions. Windows runs the actual production helpers below.
namespace convertedPixels {
inline void macGrabber(unsigned char* src, unsigned char* dst, size_t width, size_t height, size_t bytesPerRow) {
    for (size_t y = 0; y < height; y++) {
        unsigned char* srcRow = src + y * bytesPerRow;
        unsigned char* dstRow = dst + y * width * 4;
        trussc::simd::bgraToRgba(srcRow, dstRow, width, true);
    }
}
inline void macPlayer(unsigned char* baseAddress, unsigned char* _pixelBuffer, size_t copyW, size_t copyH, size_t _videoWidth, size_t bytesPerRow) {
    for (size_t y = 0; y < copyH; y++) {
        unsigned char* srcRow = baseAddress + y * bytesPerRow;
        unsigned char* dstRow = _pixelBuffer + y * (size_t)_videoWidth * 4;

        trussc::simd::bgraToRgba(srcRow, dstRow, copyW);
    }
}
inline void macRecorder(const unsigned char* rgba, uint8_t* dst, int w, int h, size_t dstStride) {
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = rgba + (size_t)y * w * 4;
        uint8_t* drow = dst + (size_t)y * dstStride;
        trussc::simd::rgbaToBgra(srow, drow, static_cast<size_t>(w));
    }
}
inline void iosGrabber(unsigned char* src, unsigned char* dst, size_t width, size_t height, size_t bytesPerRow) {
    for (size_t y = 0; y < height; y++) {
        unsigned char* srcRow = src + y * bytesPerRow;
        unsigned char* dstRow = dst + y * width * 4;
        trussc::simd::bgraToRgba(srcRow, dstRow, width, true);
    }
}
inline void iosPlayer(unsigned char* baseAddress, unsigned char* _pixelBuffer, size_t copyW, size_t copyH, size_t _videoWidth, size_t bytesPerRow) {
    for (size_t y = 0; y < copyH; y++) {
        unsigned char* srcRow = baseAddress + y * bytesPerRow;
        unsigned char* dstRow = _pixelBuffer + y * size_t(_videoWidth) * 4;

        trussc::simd::bgraToRgba(srcRow, dstRow, copyW);
    }
}
inline void iosRecorder(const unsigned char* rgba, uint8_t* dst, int w, int h, size_t dstStride) {
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = rgba + (size_t)y * w * 4;
        uint8_t* drow = dst + (size_t)y * dstStride;
        trussc::simd::rgbaToBgra(srow, drow, static_cast<size_t>(w));
    }
}
} // namespace convertedPixels

const RowSite rowSites[] = {
    {"macos_grabber",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::macGrabber(s, d, l.width, l.height, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { convertedPixels::macGrabber(s, d, l.width, l.height, l.pitch); }},
    {"macos_player",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::macPlayer(s, d, l.width, l.height, l.outputWidth, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { convertedPixels::macPlayer(s, d, l.width, l.height, l.outputWidth, l.pitch); }},
    {"macos_recorder",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::macRecorder(s, d, l.width, l.height, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { convertedPixels::macRecorder(s, d, l.width, l.height, l.pitch); }, true},
    {"ios_grabber",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::iosGrabber(s, d, l.width, l.height, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { convertedPixels::iosGrabber(s, d, l.width, l.height, l.pitch); }},
    {"ios_player",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::iosPlayer(s, d, l.width, l.height, l.outputWidth, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { convertedPixels::iosPlayer(s, d, l.width, l.height, l.outputWidth, l.pitch); }},
    {"ios_recorder",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::iosRecorder(s, d, l.width, l.height, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { convertedPixels::iosRecorder(s, d, l.width, l.height, l.pitch); }, true},
    {"windows_player_staging",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::winStaging(s, d, l.width, l.height, l.pitch); },
        [](auto s, auto d, const SiteLayout& l) { trussc::internal::copyPlayerStagingBGRA(s, d, l.width, l.height, l.pitch); }},
    {"windows_player_source_positive",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::winRGB32(s, d, l.width, l.height, static_cast<std::int32_t>(l.pitch), l.display); },
        [](auto s, auto d, const SiteLayout& l) { trussc::internal::copyPlayerRGB32(s, d, l.width, l.height, static_cast<std::int32_t>(l.pitch), l.display); }},
    {"windows_player_source_negative",
        [](auto s, auto d, const SiteLayout& l) { originalPixels::winRGB32(s, d, l.width, l.height, -static_cast<std::int32_t>(l.pitch), l.display); },
        [](auto s, auto d, const SiteLayout& l) { trussc::internal::copyPlayerRGB32(s, d, l.width, l.height, -static_cast<std::int32_t>(l.pitch), l.display); }},
};
