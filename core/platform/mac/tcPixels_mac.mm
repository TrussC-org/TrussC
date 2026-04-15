// =============================================================================
// tcPixels_mac.mm - Platform-specific image loader using macOS ImageIO
// Supports HEIC/HEIF, TIFF, JPEG 2000, and any format macOS can decode.
// =============================================================================

#include "TrussC.h"

#if defined(__APPLE__)

#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

namespace trussc {

bool Pixels::loadPlatform(const fs::path& path) {
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.string().c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];

        // Create image source
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!source) return false;

        // Create CGImage
        CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        if (!cgImage) return false;

        int w = (int)CGImageGetWidth(cgImage);
        int h = (int)CGImageGetHeight(cgImage);
        int channels = 4; // Always RGBA

        size_t size = (size_t)w * h * channels;
        auto* pixels = new unsigned char[size];

        // Draw into RGBA bitmap context
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            pixels, w, h, 8, w * channels, colorSpace,
            (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big
        );
        CGColorSpaceRelease(colorSpace);

        if (!ctx) {
            delete[] pixels;
            CGImageRelease(cgImage);
            return false;
        }

        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImage);
        CGContextRelease(ctx);
        CGImageRelease(cgImage);

        // Fill alpha channel to 0xFF (kCGImageAlphaNoneSkipLast leaves it undefined)
        for (size_t i = 3; i < size; i += 4) {
            pixels[i] = 0xFF;
        }

        // Assign to Pixels members
        clear();
        width_ = w;
        height_ = h;
        channels_ = channels;
        format_ = PixelFormat::U8;
        data_ = pixels;
        allocated_ = true;

        return true;
    }
}

} // namespace trussc

#endif
