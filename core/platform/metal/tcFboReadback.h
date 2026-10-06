// =============================================================================
// Shared Metal FBO byte readback and staging helper (macOS / iOS).
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
                               void* dstBuffer) {
    id<MTLCommandQueue> cmdQueue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    if (!cmdQueue) {
        tc::logError() << "[FBO] Failed to get Metal command queue";
        return false;
    }

    sg_mtl_image_info info = sg_mtl_query_image_info(srcImage);
    id<MTLTexture> srcTexture = (__bridge id<MTLTexture>)info.tex[info.active_slot];
    if (!srcTexture) {
        tc::logError() << "[FBO] Failed to get source MTLTexture";
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
        tc::logError() << "[FBO] Failed to create staging texture";
        return false;
    }

    // End only the active swapchain pass, preserving its attachments for LOAD.
    // Keep it suspended until the blit completes: a newly begun sokol pass
    // enqueues its command buffer, which would otherwise precede this blit.
    const bool resumeSwapchain = isInSwapchainPass();
    if (resumeSwapchain) suspendSwapchainPass();
    sg_tc_mtl_flush();

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
    [cmdBuffer waitUntilCompleted];

    if (resumeSwapchain) resumeSwapchainPass();

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [dstTexture getBytes:dstBuffer
             bytesPerRow:bytesPerRow
              fromRegion:region
             mipmapLevel:0];

    return true;
}

bool Fbo::readPixelsPlatform(unsigned char* pixels) const {
    if (!allocated_ || !pixels) return false;

    if (format_ != TextureFormat::RGBA8) {
        logError("Fbo") << "readPixels() requires RGBA8; use readPixelsFloat() for other formats";
        return false;
    }

    size_t bytesPerRow = width_ * 4;  // RGBA8
    return readPixelsInternal(curColorTex_().getImage(), width_, height_,
                              MTLPixelFormatRGBA8Unorm, bytesPerRow, pixels);
}

} // namespace trussc

#endif // __APPLE__
