// =============================================================================
// tcFbo_mac.mm - FBO pixel readback (macOS / Metal)
// =============================================================================

#include "../metal/tcFboReadback.h"

#ifdef __APPLE__

namespace trussc {

// Map sokol pixel format to Metal pixel format
static MTLPixelFormat toMTLPixelFormat(sg_pixel_format fmt) {
    switch (fmt) {
        case SG_PIXELFORMAT_RGBA8:    return MTLPixelFormatRGBA8Unorm;
        case SG_PIXELFORMAT_RGBA16F:  return MTLPixelFormatRGBA16Float;
        case SG_PIXELFORMAT_RGBA32F:  return MTLPixelFormatRGBA32Float;
        case SG_PIXELFORMAT_R8:       return MTLPixelFormatR8Unorm;
        case SG_PIXELFORMAT_R16F:     return MTLPixelFormatR16Float;
        case SG_PIXELFORMAT_R32F:     return MTLPixelFormatR32Float;
        case SG_PIXELFORMAT_RG8:      return MTLPixelFormatRG8Unorm;
        case SG_PIXELFORMAT_RG16F:    return MTLPixelFormatRG16Float;
        case SG_PIXELFORMAT_RG32F:    return MTLPixelFormatRG32Float;
        default:                      return MTLPixelFormatRGBA8Unorm;
    }
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    if (!allocated_ || !pixels) return false;

    sg_pixel_format sgFmt = curColorTex_().getPixelFormat();
    // Determine actual format (NONE means legacy RGBA8)
    if (sgFmt == SG_PIXELFORMAT_NONE) sgFmt = SG_PIXELFORMAT_RGBA8;

    MTLPixelFormat mtlFmt = toMTLPixelFormat(sgFmt);

    // For non-float formats, read as U8 then convert
    if (sgFmt == SG_PIXELFORMAT_RGBA8 || sgFmt == SG_PIXELFORMAT_R8 || sgFmt == SG_PIXELFORMAT_RG8) {
        int ch = channelCount(format_);
        std::vector<unsigned char> tmp(width_ * height_ * ch);
        size_t bytesPerRow = width_ * ch;
        bool ok = readPixelsInternal(curColorTex_().getImage(), width_, height_,
                                     mtlFmt, bytesPerRow, tmp.data());
        if (!ok) return false;
        // Convert U8 to float
        int total = width_ * height_ * ch;
        for (int i = 0; i < total; i++) {
            pixels[i] = (float)tmp[i] / 255.0f;
        }
        return true;
    }

    // Float formats: read directly
    int bpp = bytesPerPixel(format_);
    size_t bytesPerRow = width_ * bpp;

    // For 16F formats we need to read raw half-floats and convert to float
    if (sgFmt == SG_PIXELFORMAT_R16F || sgFmt == SG_PIXELFORMAT_RG16F || sgFmt == SG_PIXELFORMAT_RGBA16F) {
        int ch = channelCount(format_);
        std::vector<uint16_t> tmp(width_ * height_ * ch);
        bool ok = readPixelsInternal(curColorTex_().getImage(), width_, height_,
                                     mtlFmt, bytesPerRow, tmp.data());
        if (!ok) return false;
        // Convert half-float to float (IEEE 754 binary16)
        int total = width_ * height_ * ch;
        for (int i = 0; i < total; i++) {
            uint16_t h = tmp[i];
            uint32_t sign = (h & 0x8000) << 16;
            uint32_t exp = (h >> 10) & 0x1F;
            uint32_t mantissa = h & 0x3FF;
            uint32_t f;
            if (exp == 0) {
                if (mantissa == 0) {
                    f = sign;
                } else {
                    // Denormalized
                    exp = 1;
                    while (!(mantissa & 0x400)) { mantissa <<= 1; exp--; }
                    mantissa &= 0x3FF;
                    f = sign | ((exp + 127 - 15) << 23) | (mantissa << 13);
                }
            } else if (exp == 31) {
                f = sign | 0x7F800000 | (mantissa << 13);
            } else {
                f = sign | ((exp + 127 - 15) << 23) | (mantissa << 13);
            }
            memcpy(&pixels[i], &f, sizeof(float));
        }
        return true;
    }

    // 32F formats: direct read
    return readPixelsInternal(curColorTex_().getImage(), width_, height_,
                              mtlFmt, bytesPerRow, pixels);
}

} // namespace trussc

#endif // __APPLE__
