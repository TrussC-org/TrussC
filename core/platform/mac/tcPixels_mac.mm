// =============================================================================
// tcPixels_mac.mm - Platform-specific image loader using ImageIO
// Supports HEIC/HEIF, TIFF, JPEG 2000, and any format the OS can decode.
// Built on macOS and iOS (core/CMakeLists.txt adds it to the iOS sources).
// =============================================================================

#include "TrussC.h"
#include "impl/tcMcpImageAdmission.h"
#include <climits>

#if defined(__APPLE__)

#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

namespace trussc {

namespace mcp::internal {
namespace {
template<class T> struct ImageRef {
    T value;
    explicit ImageRef(T v) : value(v) {}
    ~ImageRef() { if (value) CFRelease(value); }
    ImageRef(const ImageRef&) = delete;
    ImageRef& operator=(const ImageRef&) = delete;
};
}

bool loadMcpPlatformImage(Pixels& pixels, const unsigned char* encoded, size_t length) {
    @autoreleasepool {
        // Borrow the already admitted immutable snapshot, never reopen the path.
        ImageRef<CFDataRef> data(CFDataCreateWithBytesNoCopy(kCFAllocatorDefault,
            encoded, CFIndex(length), kCFAllocatorNull));
        if (!data.value) return false;
        const void* keys[] = {kCGImageSourceShouldCache, kCGImageSourceShouldAllowFloat};
        const void* values[] = {kCFBooleanFalse, kCFBooleanFalse};
        ImageRef<CFDictionaryRef> options(CFDictionaryCreate(kCFAllocatorDefault,
            keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        if (!options.value) return false;
        ImageRef<CGImageSourceRef> source(CGImageSourceCreateWithData(data.value, options.value));
        if (!source.value) return false;
        // ImageIO reports pixel dimensions without decoding; caching is disabled
        // for both metadata and image creation. This path is shared by macOS/iOS.
        ImageRef<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.value, 0, options.value));
        if (!properties.value)
            throw std::runtime_error("ImageIO cannot report image size before decoding on this platform");
        auto number = [&](CFStringRef key, int64_t fallback) {
            auto value = CFDictionaryGetValue(properties.value, key);
            int64_t result = fallback;
            if (value && (CFGetTypeID(value) != CFNumberGetTypeID() ||
                !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &result)))
                return int64_t(-1);
            return result;
        };
        const auto w = number(kCGImagePropertyPixelWidth, -1);
        const auto h = number(kCGImagePropertyPixelHeight, -1);
        const auto depth = number(kCGImagePropertyDepth, 32);
        if (w <= 0 || h <= 0 || depth <= 0)
            throw std::runtime_error("ImageIO cannot report image size before decoding on this platform");
        checkAdmission(uint64_t(w), INT_MAX, AdmissionStore::Working);
        checkAdmission(uint64_t(h), INT_MAX, AdmissionStore::Working);
        // Bound (security): ImageIO samples are capped at 128 bits to bound decoded workspace; larger metadata rejects before decoding and increments the working counter.
        checkAdmission(uint64_t(depth), 128, AdmissionStore::Working);
        const auto model = CFDictionaryGetValue(properties.value, kCGImagePropertyColorModel);
        const size_t colorChannels = model && CFEqual(model, kCGImagePropertyColorModelGray) ? 1 :
            model && (CFEqual(model, kCGImagePropertyColorModelRGB) || CFEqual(model, kCGImagePropertyColorModelLab)) ? 3 : 4;
        const auto alpha = CFDictionaryGetValue(properties.value, kCGImagePropertyHasAlpha);
        const size_t sourceChannels = colorChannels + (alpha && CFEqual(alpha, kCFBooleanTrue) ? 1 : 0);
        // Count source color/alpha channels and the four-channel RGBA output.
        // ImageIO exposes no codec workspace size or allocator hook: admit a
        // conservative block-padded estimate before calling the decoder.
        reserveImageDecode(int(w), int(h), std::max<size_t>(4, sourceChannels) * ((size_t(depth) + 7) / 8));
        ImageRef<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.value, 0, options.value));
        if (!image.value || CGImageGetWidth(image.value) != size_t(w) ||
            CGImageGetHeight(image.value) != size_t(h)) return false;
        Pixels result;
        result.allocate(int(w), int(h), 4);
        ImageRef<CGColorSpaceRef> color(CGColorSpaceCreateDeviceRGB());
        if (!color.value) return false;
        ImageRef<CGContextRef> context(CGBitmapContextCreate(result.getData(), size_t(w), size_t(h),
            8, size_t(w) * 4, color.value,
            (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big));
        if (!context.value) return false;
        CGContextDrawImage(context.value, CGRectMake(0, 0, w, h), image.value);
        for (size_t i = 3, count = size_t(w) * size_t(h) * 4; i < count; i += 4)
            result.getData()[i] = 255;
        pixels = std::move(result);
        return true;
    }
}
} // namespace mcp::internal

bool Pixels::loadPlatform(const fs::path& filePath) {
    const auto path = internal::resolveLoadPath(filePath);
    if (!internal::checkLoadFile(path, "Pixels")) return false;
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:internal::pathToUtf8(path).c_str()];
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
