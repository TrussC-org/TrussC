// =============================================================================
// Shared Metal FBO byte and float readback and staging helper (macOS / iOS).
// Include from exactly one platform FBO translation unit; not a public header.
// =============================================================================
#pragma once

#include "TrussC.h"
#include "../tcFboConversion.h"

#ifdef __APPLE__
#import <Metal/Metal.h>

namespace trussc {

static bool validMetalReadImage(sg_image image) {
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) return false;
    const sg_mtl_image_info info = sg_mtl_query_image_info(image);
    return info.tex[info.active_slot] != nullptr;
}

// Internal helper: blit GPU texture to CPU-readable staging and copy bytes
static bool readPixelsInternal(sg_image srcImage, int width, int height,
                               MTLPixelFormat mtlFormat, size_t bytesPerRow,
                               void* dstBuffer, bool reportError = true) {
    if (width <= 0 || height <= 0 || !dstBuffer ||
        bytesPerRow > std::numeric_limits<size_t>::max() / size_t(height) ||
        !validMetalReadImage(srcImage)) return false;
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

// Map sokol pixel format to Metal pixel format
static MTLPixelFormat toMTLPixelFormat(sg_pixel_format fmt) {
    switch (fmt) {
        case SG_PIXELFORMAT_BGRA8:    return MTLPixelFormatBGRA8Unorm;
        case SG_PIXELFORMAT_RGBA16:   return MTLPixelFormatRGBA16Unorm;
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

bool Fbo::readPixelsPlatform(unsigned char* pixels, bool reportError) const {
    size_t rowBytes, totalBytes, outputRowBytes, outputBytes;
    if (!allocated_ || !pixels ||
        !internal::fboReadbackLayout(target_.width, target_.height, 4, outputRowBytes, outputBytes) ||
        !internal::fboReadbackLayout(target_.width, target_.height, bytesPerPixel(format_), rowBytes, totalBytes) ||
        !validMetalReadImage(curColorTex_().getImage())) return false;

    if (format_ == TextureFormat::RGBA8) {
        return readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                                  MTLPixelFormatRGBA8Unorm, rowBytes, pixels, reportError);
    }
    // Peak staging: one native-format CPU image plus one shared Metal image
    // (up to 32 * width * height bytes for RGBA32F, excluding Metal padding).
    std::vector<unsigned char> raw(totalBytes);
    if (!readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                            toMTLPixelFormat(toSokolFormat(format_)), rowBytes,
                            raw.data(), reportError)) return false;
    for (int y = 0; y < target_.height; ++y) {
        internal::fboRowToRgba8(raw.data() + size_t(y) * rowBytes,
                              pixels + size_t(y) * outputRowBytes, target_.width, format_);
    }
    return true;
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    const int ch = channelCount(format_);
    size_t bytesPerRow, nativeBytes, outputRowBytes, outputBytes;
    if (!allocated_ || !pixels ||
        !internal::fboReadbackLayout(target_.width, target_.height, size_t(ch) * sizeof(float),
                                    outputRowBytes, outputBytes) ||
        !internal::fboReadbackLayout(target_.width, target_.height, bytesPerPixel(format_),
                                    bytesPerRow, nativeBytes) ||
        !validMetalReadImage(curColorTex_().getImage())) return false;
    const size_t total = outputBytes / sizeof(float);

    sg_pixel_format sgFmt = curColorTex_().getPixelFormat();
    // Determine actual format (NONE means legacy RGBA8)
    if (sgFmt == SG_PIXELFORMAT_NONE) sgFmt = SG_PIXELFORMAT_RGBA8;

    MTLPixelFormat mtlFmt = toMTLPixelFormat(sgFmt);

    // For non-float formats, read as U8 then convert
    if (sgFmt == SG_PIXELFORMAT_RGBA8 || sgFmt == SG_PIXELFORMAT_R8 ||
        sgFmt == SG_PIXELFORMAT_RG8 || sgFmt == SG_PIXELFORMAT_BGRA8) {
        std::vector<unsigned char> tmp(nativeBytes);
        bool ok = readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                                     mtlFmt, bytesPerRow, tmp.data());
        if (!ok) return false;
        // Convert U8 to float
        for (size_t i = 0; i < total; i++) {
            const size_t source = sgFmt == SG_PIXELFORMAT_BGRA8 && i % 4 < 3 ? (i / 4) * 4 + 2 - i % 4 : i;
            pixels[i] = (float)tmp[source] / 255.0f;
        }
        return true;
    }

    if (sgFmt == SG_PIXELFORMAT_RGBA16) {
        std::vector<uint16_t> tmp(nativeBytes / sizeof(uint16_t));
        if (!readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                                mtlFmt, bytesPerRow, tmp.data())) return false;
        for (size_t i = 0; i < tmp.size(); ++i) pixels[i] = float(tmp[i]) / 65535.0f;
        return true;
    }

    // For 16F formats we need to read raw half-floats and convert to float
    if (sgFmt == SG_PIXELFORMAT_R16F || sgFmt == SG_PIXELFORMAT_RG16F || sgFmt == SG_PIXELFORMAT_RGBA16F) {
        std::vector<uint16_t> tmp(nativeBytes / sizeof(uint16_t));
        bool ok = readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                                     mtlFmt, bytesPerRow, tmp.data());
        if (!ok) return false;
        // Convert half-float to float (IEEE 754 binary16)
        for (size_t i = 0; i < total; i++) {
            pixels[i] = internal::fboHalfToFloat(tmp[i]);
        }
        return true;
    }

    // 32F formats: direct read
    return readPixelsInternal(curColorTex_().getImage(), target_.width, target_.height,
                              mtlFmt, bytesPerRow, pixels);
}

} // namespace trussc

#endif // __APPLE__
