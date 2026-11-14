#pragma once

// Shared AVFoundation preparation for the macOS/iOS VideoPlayer backends.
// Callbacks own only private preparation state, never a VideoPlayer or App.
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include "tc/video/internal/tcVideoRotation.h"

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
inline AVVideoComposition* videoDisplayComposition(AVAsset* asset, AVAssetTrack* track) {
    const CGAffineTransform preferred = track.preferredTransform;
    const auto rotation = VideoRotation::fromMatrix(preferred.a, preferred.b, preferred.c, preferred.d);
    if (!rotation.active()) return nil;
    AVMutableVideoComposition* composition = [AVMutableVideoComposition videoCompositionWithPropertiesOfAsset:asset];
    int width = int(track.naturalSize.width), height = int(track.naturalSize.height);
    const CGSize encodedSize = CGSizeMake(width, height);
    rotation.size(width, height);
    // Bound (external input): use only a quarter turn and source reflection, never the file's scale, shear or translation.
    composition.renderSize = CGSizeMake(width, height);
    CGAffineTransform transform = CGAffineTransformIdentity;
    if (rotation.degrees == 90) transform = CGAffineTransformMake(0, 1, -1, 0, 0, 0);
    else if (rotation.degrees == 180) transform = CGAffineTransformMake(-1, 0, 0, -1, 0, 0);
    else if (rotation.degrees == 270) transform = CGAffineTransformMake(0, -1, 1, 0, 0, 0);
    if (rotation.mirrored) { transform.a = -transform.a; transform.b = -transform.b; }
    const CGRect bounds = CGRectApplyAffineTransform(
        CGRectMake(0, 0, encodedSize.width, encodedSize.height), transform);
    transform.tx = -bounds.origin.x;
    transform.ty = -bounds.origin.y;
    AVMutableVideoCompositionLayerInstruction* layer =
        [AVMutableVideoCompositionLayerInstruction videoCompositionLayerInstructionWithAssetTrack:track];
    [layer setTransform:transform atTime:kCMTimeZero];
    AVMutableVideoCompositionInstruction* instruction = [AVMutableVideoCompositionInstruction videoCompositionInstruction];
    instruction.timeRange = CMTimeRangeMake(kCMTimeZero, asset.duration);
    instruction.layerInstructions = @[layer];
    composition.instructions = @[instruction];
    // Preserve the source track's timestamps, including variable frame rate.
    composition.sourceTrackIDForFrameTiming = track.trackID;
    return composition;
}

template<class State>
void prepareVideoAssetAsync(std::shared_ptr<State> state) {
    NSString* path = [NSString stringWithUTF8String:pathToUtf8(state->path).c_str()];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path]
        options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @(YES)}];
    AVAssetImageGenerator* generator = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
    generator.appliesPreferredTrackTransform = NO;
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
                return;
            }
            generator.videoComposition = videoDisplayComposition(asset, track);
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
