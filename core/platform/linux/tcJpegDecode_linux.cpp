#include "impl/tcJpegDecode.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <climits>
#include <limits>
#include <jerror.h>

namespace trussc::internal {

JpegDecode::JpegDecode() {
    info_.err = jpeg_std_error(&error_.manager);
    error_.manager.error_exit = [](j_common_ptr info) noexcept {
        auto* error = reinterpret_cast<Error*>(info->err);
        std::longjmp(error->jump, 1);
    };
    // Warnings are recoverable, and must not write directly to stderr.
    error_.manager.emit_message = [](j_common_ptr, int) noexcept {};
}

JpegDecode::~JpegDecode() { jpeg_destroy_decompress(&info_); }

bool JpegDecode::open(const unsigned char* bytes, size_t length) {
    // Bound (decoder ABI): jpeg_mem_src takes an unsigned long byte count.
    if (!isJpeg(bytes, length) || length > ULONG_MAX) return false;
    if (setjmp(error_.jump)) return false;
    jpeg_create_decompress(&info_);
    jpeg_mem_src(&info_, bytes, static_cast<unsigned long>(length));
    // jpeg_mem_src normally inserts a synthetic EOI at EOF. A bounded input
    // snapshot must instead reject truncation, without making warnings fatal.
    info_.src->fill_input_buffer = [](j_decompress_ptr info) noexcept -> boolean {
        info->err->msg_code = JERR_INPUT_EOF;
        info->err->error_exit(reinterpret_cast<j_common_ptr>(info));
        return FALSE;
    };
    if (jpeg_read_header(&info_, TRUE) != JPEG_HEADER_OK) return false;
    needsStbFallback_ = info_.jpeg_color_space == JCS_CMYK || info_.jpeg_color_space == JCS_YCCK;
    if (needsStbFallback_) {
        // Image loading retries the original bytes with stb; cameras reject CMYK.
        jpeg_destroy_decompress(&info_);
        return false;
    }
    return info_.image_width > 0 && info_.image_width <= INT_MAX &&
           info_.image_height > 0 && info_.image_height <= INT_MAX;
}

bool JpegDecode::decode(unsigned char* rgba, size_t capacity) {
    // Bound (abi): the caller's RGBA byte product must fit size_t and storage.
    if (!rgba || !info_.image_width || !info_.image_height ||
        size_t(info_.image_width) > std::numeric_limits<size_t>::max() / 4 / info_.image_height ||
        size_t(info_.image_width) * info_.image_height * 4 > capacity) return false;
    if (setjmp(error_.jump)) return false;
    info_.out_color_space = JCS_EXT_RGBA;
    if (!jpeg_start_decompress(&info_)) return false;
    while (info_.output_scanline < info_.output_height) {
        auto* row = rgba + size_t(info_.output_scanline) * info_.output_width * 4;
        if (jpeg_read_scanlines(&info_, &row, 1) != 1) return false;
    }
    return jpeg_finish_decompress(&info_) != FALSE;
}

} // namespace trussc::internal
#endif
