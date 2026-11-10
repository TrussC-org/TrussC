#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>
#include "impl/tcJpegDecode.h"

namespace jpegTest {
inline std::vector<unsigned char> encode(int width, int height, bool gray = false, bool progressive = false) {
    const int channels = gray ? 1 : 3;
    std::vector<unsigned char> pixels(size_t(width) * height * channels);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const size_t i = (size_t(y) * width + x) * channels;
        pixels[i] = static_cast<unsigned char>(x * 255 / width);
        if (!gray) {
            pixels[i + 1] = static_cast<unsigned char>(y * 255 / height);
            pixels[i + 2] = static_cast<unsigned char>((x + y) % 256);
        }
    }
    jpeg_compress_struct info{};
    jpeg_error_mgr error{};
    info.err = jpeg_std_error(&error);
    jpeg_create_compress(&info);
    unsigned char* bytes = nullptr;
    unsigned long length = 0;
    jpeg_mem_dest(&info, &bytes, &length);
    info.image_width = width;
    info.image_height = height;
    info.input_components = channels;
    info.in_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 85, TRUE);
    if (progressive) jpeg_simple_progression(&info);
    jpeg_start_compress(&info, TRUE);
    while (info.next_scanline < info.image_height) {
        auto* row = pixels.data() + size_t(info.next_scanline) * width * channels;
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    std::vector<unsigned char> result(bytes, bytes + length);
    std::free(bytes);
    jpeg_destroy_compress(&info);
    return result;
}

// Small CMYK/YCCK fixture from the #1389 review probe.
inline std::vector<unsigned char> cmyk(bool adobe, bool ycck) {
    jpeg_compress_struct j{};
    jpeg_error_mgr err{};
    j.err = jpeg_std_error(&err);
    jpeg_create_compress(&j);
    unsigned char* bytes = nullptr;
    unsigned long length = 0;
    jpeg_mem_dest(&j, &bytes, &length);
    j.image_width = 8; j.image_height = 8;
    j.input_components = 4; j.in_color_space = JCS_CMYK;
    jpeg_set_defaults(&j);
    if (ycck) jpeg_set_colorspace(&j, JCS_YCCK);
    j.write_Adobe_marker = adobe;
    jpeg_set_quality(&j, 95, TRUE);
    jpeg_start_compress(&j, TRUE);
    std::array<unsigned char, 8 * 4> row{};
    for (size_t x = 0; x < 8; ++x) {
        row[x * 4] = 40; row[x * 4 + 1] = 100;
        row[x * 4 + 2] = 180; row[x * 4 + 3] = 210;
    }
    while (j.next_scanline < j.image_height) {
        auto* p = row.data(); jpeg_write_scanlines(&j, &p, 1);
    }
    jpeg_finish_compress(&j);
    std::vector<unsigned char> out(bytes, bytes + length);
    std::free(bytes); jpeg_destroy_compress(&j);
    return out;
}

// Patch SOF dimensions without allocating the declared pixel storage.
inline std::vector<unsigned char> withDimensions(std::vector<unsigned char> bytes, int width, int height) {
    for (size_t i = 2; i + 9 < bytes.size();) {
        if (bytes[i + 1] == 0xc0) {
            bytes[i + 5] = height >> 8; bytes[i + 6] = height & 255;
            bytes[i + 7] = width >> 8; bytes[i + 8] = width & 255;
            return bytes;
        }
        i += 2 + size_t(bytes[i + 2]) * 256 + bytes[i + 3];
    }
    return {};
}

// Strip DHT segments before the first scan, as the #1369 camera test does.
inline std::vector<unsigned char> stripDht(const std::vector<unsigned char>& jpeg) {
    std::vector<unsigned char> result(jpeg.begin(), jpeg.begin() + 2);
    size_t pos = 2;
    while (pos + 4 <= jpeg.size() && jpeg[pos + 1] != 0xda) {
        const size_t length = 2 + (size_t(jpeg[pos + 2]) << 8) + jpeg[pos + 3];
        if (jpeg[pos + 1] != 0xc4)
            result.insert(result.end(), jpeg.begin() + pos, jpeg.begin() + pos + length);
        pos += length;
    }
    result.insert(result.end(), jpeg.begin() + pos, jpeg.end());
    return result;
}
}
