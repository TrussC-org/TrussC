#pragma once

// Shared AVFoundation preparation for the macOS/iOS VideoPlayer backends.
// Callbacks own only private preparation state, never a VideoPlayer or App.
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>

namespace trussc::internal {

inline bool videoPosterPixels(CGImageRef image, Pixels& pixels) {
    if (!image) return false;
    const size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    size_t bytes = 0, span = 0;
    if (!VideoDecodeLimits::rgba(int64_t(w), int64_t(h), bytes) ||
        !VideoDecodeLimits::product(CGImageGetBytesPerRow(image), h, span)) return false;
    try { pixels.allocate(int(w), int(h), 4); }
    catch (const std::bad_alloc&) { return false; }
    // Match the synchronous poster's color space and RGBA bitmap layout.
    CGColorSpaceRef source = CGImageGetColorSpace(image);
    const bool rgb = source && CGColorSpaceGetModel(source) == kCGColorSpaceModelRGB;
    CGColorSpaceRef colorSpace = rgb ? CGColorSpaceRetain(source) : CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels.getData(), w, h, 8, w * 4,
        colorSpace, (CGBitmapInfo)kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (context) {
        CGContextDrawImage(context, CGRectMake(0, 0, w, h), image);
        CGContextRelease(context);
    } else pixels.clear();
    CGColorSpaceRelease(colorSpace);
    return context != nullptr;
}

// Keep the completion-handler APIs available on the same older Apple targets
// as load(). The async/await replacements require newer SDK/runtime versions.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
template<class State>
void prepareVideoAssetAsync(std::shared_ptr<State> state) {
    NSString* path = [NSString stringWithUTF8String:pathToUtf8(state->path).c_str()];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path]
        options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @(YES)}];
    AVAssetImageGenerator* generator = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
    generator.appliesPreferredTrackTransform = YES;
    generator.requestedTimeToleranceBefore = kCMTimePositiveInfinity;
    generator.requestedTimeToleranceAfter = kCMTimeZero;
    state->asset = std::shared_ptr<void>((__bridge_retained void*)asset,
        [](void* retained) { CFRelease(retained); });
    state->cancelBackend = [asset, generator] {
        [asset cancelLoading];
        [generator cancelAllCGImageGeneration];
    };
    [asset loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration"] completionHandler:^{
        if (state->cancelled.load()) return;
        const bool prepared = prepareResourceLoad(*state, [&] {
            if ([asset statusOfValueForKey:@"tracks" error:nullptr] != AVKeyValueStatusLoaded ||
                [asset statusOfValueForKey:@"duration" error:nullptr] != AVKeyValueStatusLoaded) {
                state->result = LoadResult::fail(LoadError::DecodeFailed, "failed to load video tracks or duration: " + pathToDisplayUtf8(state->path));
                return;
            }
            // Bound poster decoding before asking AVFoundation to allocate it.
            AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
            const CGSize size = track.naturalSize;
            size_t bytes = 0;
            if (!track || !std::isfinite(size.width) || !std::isfinite(size.height) ||
                size.width <= 0 || size.height <= 0 || size.width > INT_MAX / 4 || size.height > INT_MAX ||
                !VideoDecodeLimits::rgba(int64_t(size.width), int64_t(size.height), bytes)) {
                state->result = LoadResult::fail(LoadError::DecodeFailed, "invalid video dimensions or missing video track: " + pathToDisplayUtf8(state->path));
            }
        });
        if (!prepared || !state->result || !state->autoPoster) {
            if (!state->cancelled.load()) completeResourceLoad(state);
            return;
        }
        [generator generateCGImagesAsynchronouslyForTimes:@[[NSValue valueWithCMTime:kCMTimeZero]]
            completionHandler:^(CMTime requestedTime, CGImageRef image, CMTime actualTime,
                                 AVAssetImageGeneratorResult result, NSError* error) {
                (void)requestedTime; (void)actualTime; (void)error;
                if (state->cancelled.load()) return;
                // Like load(), a failed optional poster leaves a cleared texture.
                if (result == AVAssetImageGeneratorSucceeded) videoPosterPixels(image, state->poster);
                completeResourceLoad(state);
            }];
    }];
}
#pragma clang diagnostic pop

} // namespace trussc::internal
