// =============================================================================
// tcFbo_mac.mm - FBO のプラットフォーム固有実装（macOS / Metal）
// =============================================================================

#include "TrussC.h"

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace trussc {

bool Fbo::readPixelsPlatform(unsigned char* pixels) const {
    if (!allocated_ || !pixels) return false;

    // sokol から Metal デバイスを取得
    id<MTLDevice> device = (__bridge id<MTLDevice>)sg_mtl_device();

    if (!device) {
        printf("[FBO] Failed to get Metal device\n");
        return false;
    }

    // コマンドキューを作成
    id<MTLCommandQueue> cmdQueue = [device newCommandQueue];
    if (!cmdQueue) {
        printf("[FBO] Failed to create command queue\n");
        return false;
    }

    // ソーステクスチャを取得
    sg_mtl_image_info info = sg_mtl_query_image_info(colorImage_);
    id<MTLTexture> srcTexture = (__bridge id<MTLTexture>)info.tex[info.active_slot];

    if (!srcTexture) {
        printf("[FBO] Failed to get source MTLTexture\n");
        return false;
    }

    // 一時的な Shared テクスチャを作成（CPU から読み取り可能）
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                    width:width_
                                                                                   height:height_
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> dstTexture = [device newTextureWithDescriptor:desc];
    if (!dstTexture) {
        printf("[FBO] Failed to create destination texture\n");
        return false;
    }

    // Blit コマンドでコピー
    id<MTLCommandBuffer> cmdBuffer = [cmdQueue commandBuffer];
    id<MTLBlitCommandEncoder> blitEncoder = [cmdBuffer blitCommandEncoder];

    [blitEncoder copyFromTexture:srcTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width_, height_, 1)
                       toTexture:dstTexture
                destinationSlice:0
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, 0, 0)];

    [blitEncoder endEncoding];
    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];

    // Shared テクスチャからピクセルを読み取る
    MTLRegion region = MTLRegionMake2D(0, 0, width_, height_);
    NSUInteger bytesPerRow = width_ * 4;  // RGBA8

    [dstTexture getBytes:pixels
             bytesPerRow:bytesPerRow
              fromRegion:region
             mipmapLevel:0];

    return true;
}

} // namespace trussc

#endif // __APPLE__
