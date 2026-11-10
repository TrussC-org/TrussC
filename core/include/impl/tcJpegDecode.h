#pragma once

#if defined(__linux__) && !defined(__ANDROID__)
#include <cstddef>
#include <cstdio>
#include <csetjmp>
#include <jpeglib.h>

namespace trussc::internal {

inline bool isJpeg(const unsigned char* bytes, size_t length) {
    return bytes && length >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff;
}

// One decoder per image/frame. The caller owns the RGBA storage; all libjpeg
// allocations are released even after a callback longjmp. No objects with
// nontrivial destructors are created between setjmp and a libjpeg call that
// can jump back to it.
class JpegDecode {
public:
    JpegDecode();
    ~JpegDecode();
    JpegDecode(const JpegDecode&) = delete;
    JpegDecode& operator=(const JpegDecode&) = delete;
    bool open(const unsigned char* bytes, size_t length);
    int width() const { return static_cast<int>(info_.image_width); }
    int height() const { return static_cast<int>(info_.image_height); }
    bool decode(unsigned char* rgba, size_t capacity);
private:
    struct Error {
        jpeg_error_mgr manager;
        std::jmp_buf jump;
    } error_{};
    jpeg_decompress_struct info_{};
};

} // namespace trussc::internal
#endif
