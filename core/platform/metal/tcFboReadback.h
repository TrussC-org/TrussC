// =============================================================================
// Shared Metal FBO byte and float readback and staging helper (macOS / iOS).
// Include from exactly one platform FBO translation unit; not a public header.
// =============================================================================
#pragma once

#include "TrussC.h"

#ifdef __APPLE__
#import <Metal/Metal.h>

namespace trussc {

// Internal helper: blit GPU texture to CPU-readable staging and copy bytes
static bool readPixelsInternal(sg_image srcImage, int width, int height,
                               MTLPixelFormat mtlFormat, size_t bytesPerRow,
                               void* dstBuffer, bool reportError = true) {
    id<MTLCommandQueue> cmdQueue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    if (!cmdQueue) {
        if (reportError) tc::logError() << "[FBO] Failed to get Metal command queue";
        return false;
    }

    sg_mtl_image_info info = sg_mtl_query_image_info(srcImage);
    id<MTLTexture> srcTexture = (__bridge id<MTLTexture>)info.tex[info.active_slot];
    if (!srcTexture) {
        if (reportError) tc::logError() << "[FBO] Failed to get source MTLTexture";
        return false;
    }

    id<MTLDevice> device = cmdQueue.device;

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlFormat
                                                                                    width:width
                                                                                   height:height
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> dstTexture = [device newTextureWithDescriptor:desc];
    if (!dstTexture) {
        if (reportError) tc::logError() << "[FBO] Failed to create staging texture";
        return false;
    }

    // Native transfers park the current target without fabricating a raster
    // pass. Submit sokol before the blit, then wait before the parent's resume.
    internal::RenderTarget transfer;
    transfer.passKind = internal::TargetPassKind::Transfer;
    transfer.width = width;
    transfer.height = height;
    transfer.colorFormat = sg_query_image_desc(srcImage).pixel_format;
    transfer.sampleCount = 1;
    transfer.transferSource = srcImage;
    transfer.hasDepth = false;
    transfer.depthFormat = SG_PIXELFORMAT_NONE;
    transfer.beginTransfer = [](void*) { sg_tc_mtl_flush(); };
    transfer.endTransfer = [](void* buffer) {
        // Balance the explicit retain even if ARC shortens the caller's local
        // variable lifetime. Completion must precede a parent's new encoder.
        id<MTLCommandBuffer> command = (__bridge_transfer id<MTLCommandBuffer>)buffer;
        [command waitUntilCompleted];
    };
    {
        internal::TargetPassScope scope(transfer);
        if (!scope.active()) return false;

        id<MTLCommandBuffer> cmdBuffer = [cmdQueue commandBuffer];
        id<MTLBlitCommandEncoder> blitEncoder = [cmdBuffer blitCommandEncoder];

        [blitEncoder copyFromTexture:srcTexture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                           toTexture:dstTexture
                    destinationSlice:0
                    destinationLevel:0
                   destinationOrigin:MTLOriginMake(0, 0, 0)];

        [blitEncoder endEncoding];
        [cmdBuffer commit];
        transfer.owner = (__bridge_retained void*)cmdBuffer;
    } // waits for the blit, then resumes the previous target

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [dstTexture getBytes:dstBuffer
             bytesPerRow:bytesPerRow
              fromRegion:region
             mipmapLevel:0];

    return true;
}

bool Fbo::readPixelsPlatform(unsigned char* pixels, bool reportError) const {
    if (!allocated_ || !pixels) return false;

    if (format_ != TextureFormat::RGBA8) {
        if (reportError) logError("Fbo") << "readPixels() requires RGBA8; use readPixelsFloat() for other formats";
        return false;
    }

    size_t bytesPerRow = target_.width * 4;  // RGBA8
    return readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                              MTLPixelFormatRGBA8Unorm, bytesPerRow, pixels, reportError);
}

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
        std::vector<unsigned char> tmp(target_.width * target_.height * ch);
        size_t bytesPerRow = target_.width * ch;
        bool ok = readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                                     mtlFmt, bytesPerRow, tmp.data());
        if (!ok) return false;
        // Convert U8 to float
        int total = target_.width * target_.height * ch;
        for (int i = 0; i < total; i++) {
            pixels[i] = (float)tmp[i] / 255.0f;
        }
        return true;
    }

    // Float formats: read directly
    int bpp = bytesPerPixel(format_);
    size_t bytesPerRow = target_.width * bpp;

    // For 16F formats we need to read raw half-floats and convert to float
    if (sgFmt == SG_PIXELFORMAT_R16F || sgFmt == SG_PIXELFORMAT_RG16F || sgFmt == SG_PIXELFORMAT_RGBA16F) {
        int ch = channelCount(format_);
        std::vector<uint16_t> tmp(target_.width * target_.height * ch);
        bool ok = readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                                     mtlFmt, bytesPerRow, tmp.data());
        if (!ok) return false;
        // Convert half-float to float (IEEE 754 binary16)
        int total = target_.width * target_.height * ch;
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
    return readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                              mtlFmt, bytesPerRow, pixels);
}

} // namespace trussc

#endif // __APPLE__
