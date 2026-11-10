#pragma once
#include <algorithm>
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
