// =============================================================================
// tcVideoRecorder_ios.mm - iOS AVFoundation (AVAssetWriter) implementation
// =============================================================================
// Implements the VideoWriter platform hooks from tc/video/tcVideoRecorder.h.
// AVAssetWriter is shared between macOS and iOS, so this mirrors
// platform/mac/tcVideoRecorder_mac.mm (kept as a separate file to match the
// per-platform layout of the video player). H.264 / HEVC encode in hardware;
// ProRes is accepted only where the OS provides it (canAddInput gates it).
// =============================================================================

#if defined(__APPLE__)
#import <TargetConditionals.h>
#if TARGET_OS_IOS

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>

#include "TrussC.h"
#include <cmath>

namespace trussc {

namespace internal {
struct VideoWriterPlatformData {
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* input = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    int width = 0;
    int height = 0;
    double fps = 30.0;
    int64_t frameIndex = 0;
    bool failed = false;
};
}  // namespace internal
using internal::VideoWriterPlatformData;

bool VideoWriter::openPlatform(const std::string& fullPath, int w, int h,
                               float fps, const VideoRecordSettings& settings) {
    if (settings.audio) {
        logWarning("VideoWriter")
            << "audio recording is not supported on this platform yet - "
               "recording video only";
    }
    @autoreleasepool {
        NSString* codecKey = AVVideoCodecTypeH264;
        AVFileType fileType = AVFileTypeMPEG4;
        bool useBitrate = true;
        switch (settings.codec) {
            case VideoCodec::H264:
                codecKey = AVVideoCodecTypeH264; break;
            case VideoCodec::HEVC:
                codecKey = AVVideoCodecTypeHEVC; break;
            case VideoCodec::ProRes422:
                codecKey = AVVideoCodecTypeAppleProRes422;
                fileType = AVFileTypeQuickTimeMovie; useBitrate = false; break;
            case VideoCodec::ProRes4444:
                codecKey = AVVideoCodecTypeAppleProRes4444;
                fileType = AVFileTypeQuickTimeMovie; useBitrate = false; break;
        }
        if (fileType == AVFileTypeQuickTimeMovie &&
            fullPath.size() >= 4 &&
            fullPath.compare(fullPath.size() - 4, 4, ".mov") != 0) {
            logWarning("VideoWriter")
                << "ProRes is written as QuickTime; prefer a .mov path ("
                << fullPath << ")";
        }

        NSString* nsPath = [NSString stringWithUTF8String:fullPath.c_str()];
        // The app bundle is read-only on iOS, and getDataPath() resolves there.
        // Redirect bundle-relative outputs to the writable Documents directory
        // so startRecording("clip.mp4") just works on device.
        NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
        if ([nsPath hasPrefix:bundlePath]) {
            NSString* docs = [NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
            if (docs) {
                nsPath = [docs stringByAppendingPathComponent:[nsPath lastPathComponent]];
                logNotice("VideoWriter")
                    << "iOS: writing to Documents -> " << nsPath.UTF8String;
            }
        }
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

        NSError* err = nil;
        AVAssetWriter* writer =
            [[AVAssetWriter alloc] initWithURL:url fileType:fileType error:&err];
        if (!writer) {
            logError("VideoWriter")
                << "AVAssetWriter init failed: "
                << (err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }

        NSMutableDictionary* outSettings = [@{
            AVVideoCodecKey: codecKey,
            AVVideoWidthKey: @(w),
            AVVideoHeightKey: @(h),
        } mutableCopy];

        if (useBitrate) {
            int br = (settings.bitrate > 0)
                         ? settings.bitrate
                         : (int)((double)w * h * (fps > 0 ? fps : 30.0) * 0.1);
            NSMutableDictionary* compression =
                [@{ AVVideoAverageBitRateKey: @(br) } mutableCopy];
            if (settings.keyframeInterval > 0) {
                compression[AVVideoMaxKeyFrameIntervalKey] = @(settings.keyframeInterval);
            }
            outSettings[AVVideoCompressionPropertiesKey] = compression;
        }

        AVAssetWriterInput* input =
            [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                               outputSettings:outSettings];
        input.expectsMediaDataInRealTime = NO;

        NSDictionary* sourceAttrs = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(w),
            (NSString*)kCVPixelBufferHeightKey: @(h),
            (NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        };
        AVAssetWriterInputPixelBufferAdaptor* adaptor =
            [AVAssetWriterInputPixelBufferAdaptor
                assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                            sourcePixelBufferAttributes:sourceAttrs];

        if (![writer canAddInput:input]) {
            logError("VideoWriter") << "writer cannot add video input (codec "
                                    << videoCodecName(settings.codec) << ")";
            return false;
        }
        [writer addInput:input];

        if (![writer startWriting]) {
            logError("VideoWriter")
                << "startWriting failed: "
                << (writer.error ? writer.error.localizedDescription.UTF8String
                                 : "unknown");
            return false;
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        auto* pd = new VideoWriterPlatformData();
        pd->writer = writer;
        pd->input = input;
        pd->adaptor = adaptor;
        pd->width = w;
        pd->height = h;
        pd->fps = (fps > 0) ? fps : 30.0;
        platform_ = pd;
        return true;
    }
}

// Audio track is macOS-only for now; the header degrades to video-only.
bool VideoWriter::appendAudioPlatform(const float*, int, double) { return false; }

bool VideoWriter::appendPlatform(const unsigned char* rgba, double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->writer || pd->failed) return false;

    @autoreleasepool {
        int spins = 0;
        while (!pd->input.readyForMoreMediaData && spins < 5000) {
            [NSThread sleepForTimeInterval:0.001];
            ++spins;
        }
        if (!pd->input.readyForMoreMediaData) {
            logError("VideoWriter") << "input not ready (timeout)";
            pd->failed = true;
            return false;
        }

        CVPixelBufferRef pb = NULL;
        CVPixelBufferPoolRef pool = pd->adaptor.pixelBufferPool;
        if (pool) {
            CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &pb);
        }
        if (!pb) {
            NSDictionary* attrs = @{
                (NSString*)kCVPixelBufferCGImageCompatibilityKey: @YES,
                (NSString*)kCVPixelBufferCGBitmapContextCompatibilityKey: @YES,
                (NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
            };
            CVPixelBufferCreate(kCFAllocatorDefault, pd->width, pd->height,
                                kCVPixelFormatType_32BGRA,
                                (__bridge CFDictionaryRef)attrs, &pb);
        }
        if (!pb) {
            logError("VideoWriter") << "could not create pixel buffer";
            pd->failed = true;
            return false;
        }

        CVPixelBufferLockBaseAddress(pb, 0);
        uint8_t* dst = (uint8_t*)CVPixelBufferGetBaseAddress(pb);
        size_t dstStride = CVPixelBufferGetBytesPerRow(pb);
        const int w = pd->width;
        const int h = pd->height;
        for (int y = 0; y < h; ++y) {
            const unsigned char* srow = rgba + (size_t)y * w * 4;
            uint8_t* drow = dst + (size_t)y * dstStride;
            for (int x = 0; x < w; ++x) {
                drow[x * 4 + 0] = srow[x * 4 + 2];  // B
                drow[x * 4 + 1] = srow[x * 4 + 1];  // G
                drow[x * 4 + 2] = srow[x * 4 + 0];  // R
                drow[x * 4 + 3] = srow[x * 4 + 3];  // A
            }
        }
        CVPixelBufferUnlockBaseAddress(pb, 0);

        CMTime t = CMTimeMakeWithSeconds(timeSec, 600);
        BOOL ok = [pd->adaptor appendPixelBuffer:pb withPresentationTime:t];
        CVPixelBufferRelease(pb);

        if (!ok) {
            logError("VideoWriter")
                << "appendPixelBuffer failed: "
                << (pd->writer.error
                        ? pd->writer.error.localizedDescription.UTF8String
                        : "unknown");
            pd->failed = true;
            return false;
        }
        ++pd->frameIndex;
        return true;
    }
}

void VideoWriter::closePlatform() {
    VideoWriterPlatformData* pd = platform_;
    if (!pd) return;

    @autoreleasepool {
        if (pd->writer && pd->writer.status == AVAssetWriterStatusWriting) {
            [pd->input markAsFinished];
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [pd->writer finishWritingWithCompletionHandler:^{
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        }
        pd->writer = nil;
        pd->input = nil;
        pd->adaptor = nil;
    }

    delete pd;
    platform_ = nullptr;
}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
