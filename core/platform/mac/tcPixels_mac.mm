// =============================================================================
// ImageIO fallback shared by macOS and iOS; keeps HEIC/HEIF, TIFF and JPEG 2000.
// =============================================================================
#include "TrussC.h"
#include "impl/tcImageDecode.h"
#include "impl/tcMcpImageAdmission.h"
#include <climits>

#if defined(__APPLE__)
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

namespace trussc {
namespace {
template<class T> struct ImageRef {
    T value;
    explicit ImageRef(T v) : value(v) {}
    ~ImageRef() { if (value) CFRelease(value); }
    ImageRef(const ImageRef&) = delete;
    ImageRef& operator=(const ImageRef&) = delete;
};

LoadResult decodeImageIO(Pixels& pixels, const unsigned char* encoded, size_t length, bool mcpAdmission) {
    LoadResult admitted;
    if (mcpAdmission) {
        admitted = internal::admitImageEncodedSize(length);
        if (!admitted) return admitted;
    }
    if (!encoded || !length) return LoadResult::fail(LoadError::DecodeFailed, "empty ImageIO input");
    @autoreleasepool {
        ImageRef<CFDataRef> data(CFDataCreateWithBytesNoCopy(kCFAllocatorDefault,
            encoded, CFIndex(length), kCFAllocatorNull));
        if (!data.value) return internal::imageDecodeOutOfMemory();
        const void* keys[] = {kCGImageSourceShouldCache, kCGImageSourceShouldAllowFloat};
        const void* values[] = {kCFBooleanFalse, kCFBooleanFalse};
        ImageRef<CFDictionaryRef> options(CFDictionaryCreate(kCFAllocatorDefault,
            keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        if (!options.value) return internal::imageDecodeOutOfMemory();
        ImageRef<CGImageSourceRef> source(CGImageSourceCreateWithData(data.value, options.value));
        if (!source.value) {
            try { getLogger().log(LogLevel::Error, "[Pixels] ImageIO source creation failed (unsupported data or allocation)"); } catch (...) {}
            return LoadResult::fail(LoadError::DecodeFailed, "ImageIO rejected image source");
        }
        // Metadata only, before CGImage creation; disable eager caching.
        ImageRef<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.value, 0, options.value));
        if (!properties.value) {
            try { getLogger().log(LogLevel::Error, "[Pixels] ImageIO metadata creation failed (decode or allocation)"); } catch (...) {}
            return LoadResult::fail(LoadError::DecodeFailed, "ImageIO cannot report image size");
        }
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
        if (mcpAdmission) {
            admitted = internal::admitImageDimensions(w, h, 4, 1);
            if (!admitted) return admitted;
        }
        size_t elements = 0;
        if (w <= 0 || h <= 0 || w > INT_MAX || h > INT_MAX ||
            !internal::pixelBufferCount(int(w), int(h), 4, 1, SIZE_MAX, elements))
            return LoadResult::fail(LoadError::DecodeFailed, "invalid ImageIO dimensions");
        if (depth <= 0) return LoadResult::fail(LoadError::DecodeFailed, "invalid ImageIO sample depth");
        const auto model = CFDictionaryGetValue(properties.value, kCGImagePropertyColorModel);
        const size_t colorChannels = model && CFEqual(model, kCGImagePropertyColorModelGray) ? 1 :
            model && (CFEqual(model, kCGImagePropertyColorModelRGB) || CFEqual(model, kCGImagePropertyColorModelLab)) ? 3 : 4;
        const auto alpha = CFDictionaryGetValue(properties.value, kCGImagePropertyHasAlpha);
        const size_t sourceChannels = colorChannels + (alpha && CFEqual(alpha, kCFBooleanTrue) ? 1 : 0);
        const uint64_t sampleBytes = uint64_t(depth) / 8 + (uint64_t(depth) % 8 != 0);
        if (sampleBytes > SIZE_MAX) return internal::rejectImageDecode(LoadError::TooLarge, "ImageIO sample size exceeds size_t capacity");
        if (mcpAdmission) {
            admitted = internal::admitImageDimensions(w, h, sourceChannels, size_t(sampleBytes));
            if (!admitted) return admitted;
            // Preserve the existing, stricter MCP request-owner workspace policy.
            // Bound (security): MCP ImageIO samples fit its admitted workspace. Overflow rejects the whole image and is counted by MCP.
            mcp::internal::checkAdmission(uint64_t(depth), 128, mcp::internal::AdmissionStore::Working);
            mcp::internal::reserveImageDecode(int(w), int(h), std::max<size_t>(4, sourceChannels) * size_t(sampleBytes));
        }
        // ImageIO does not expose its internal codec allocator or workspace size.
        ImageRef<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.value, 0, options.value));
        if (!image.value) {
            try { getLogger().log(LogLevel::Error, "[Pixels] ImageIO image creation failed (decode or allocation)"); } catch (...) {}
            return LoadResult::fail(LoadError::DecodeFailed, "ImageIO image creation failed");
        }
        const size_t actualW = CGImageGetWidth(image.value), actualH = CGImageGetHeight(image.value);
        if (actualW != size_t(w) || actualH != size_t(h))
            return LoadResult::fail(LoadError::DecodeFailed, "ImageIO dimensions changed during decode");
        const size_t rowBytes = CGImageGetBytesPerRow(image.value);
        const size_t bitsPerPixel = CGImageGetBitsPerPixel(image.value);
        // Bound (abi): SDK row strides and packed pixel products must fit size_t before any copy. Overflow rejects the whole image and is reported by LoadResult.
        if (!bitsPerPixel) return LoadResult::fail(LoadError::DecodeFailed, "invalid ImageIO pixel depth");
        if (actualW > (SIZE_MAX - 7) / bitsPerPixel || rowBytes > SIZE_MAX / actualH)
            return internal::rejectImageDecode(LoadError::TooLarge,
                ("ImageIO pixel store " + std::to_string(actualW) + "x" + std::to_string(actualH) +
                 ", row bytes " + std::to_string(rowBytes) + ", bits per pixel " + std::to_string(bitsPerPixel) +
                 " exceeds size_t capacity " + std::to_string(SIZE_MAX)).c_str());
        if (rowBytes < (actualW * bitsPerPixel + 7) / 8)
            return LoadResult::fail(LoadError::DecodeFailed, "invalid ImageIO row stride");
        Pixels result;
        internal::ImageDecodeAllocationScope allocation;
        if (!internal::ImageLoadAccess::allocate(result, int(w), int(h))) return internal::imageDecodeOutOfMemory();
        ImageRef<CGColorSpaceRef> color(CGColorSpaceCreateDeviceRGB());
        if (!color.value) return internal::imageDecodeOutOfMemory();
        ImageRef<CGContextRef> context(CGBitmapContextCreate(result.getData(), actualW, actualH,
            8, actualW * 4, color.value,
            (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big));
        if (!context.value) return internal::imageDecodeOutOfMemory();
        CGContextDrawImage(context.value, CGRectMake(0, 0, w, h), image.value);
        for (size_t i = 3, count = result.getTotalBytes(); i < count; i += 4) result.getData()[i] = 255;
        pixels = std::move(result);
        return LoadResult::success();
    }
}
} // namespace

LoadResult internal::decodePlatformImage(Pixels& pixels, const unsigned char* bytes, size_t length) {
    try { return decodeImageIO(pixels, bytes, length, false); }
    catch (const std::bad_alloc&) { return imageDecodeOutOfMemory(); }
}
bool mcp::internal::loadMcpPlatformImage(Pixels& pixels, const unsigned char* encoded, size_t length) {
    try {
        const auto result = decodeImageIO(pixels, encoded, length, true);
        return bool(result);
    } catch (const std::bad_alloc&) {
        trussc::internal::imageDecodeOutOfMemory();
        return false;
    }
}
bool Pixels::loadPlatform(const fs::path& filePath) {
    try {
        std::unique_ptr<unsigned char[]> bytes;
        size_t length = 0;
        auto result = internal::readEncodedImage(internal::resolveLoadPath(filePath), bytes, length);
        if (result) result = internal::decodePlatformImage(*this, bytes.get(), length);
        return bool(result);
    } catch (const std::bad_alloc&) {
        internal::imageDecodeOutOfMemory();
        return false;
    }
}
} // namespace trussc
#endif
