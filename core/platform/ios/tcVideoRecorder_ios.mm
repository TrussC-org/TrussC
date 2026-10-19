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
#include <chrono>
#include "../metal/tcRecorderPixelBuffer.h"
#include <cmath>

#include "../metal/tcVideoWriterError.h"

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
            toLower(getFileExtension(fullPath)) != "mov") {
            logWarning("VideoWriter")
                << "ProRes is written as QuickTime; prefer a .mov path ("
                << fullPath << ")";
        }

        // The path is used as given: VideoWriter::open() already refused a
        // path inside the app bundle (record to getUserDataPath() instead).
        NSString* nsPath = [NSString stringWithUTF8String:fullPath.c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

        NSError* err = nil;
        AVAssetWriter* writer =
            [[AVAssetWriter alloc] initWithURL:url fileType:fileType error:&err];
        if (!writer) {
            result_ = internal::videoOpenFailure(err, "AVAssetWriter init failed");
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
            result_ = SaveResult::fail(SaveError::WriteFailed, std::string("writer cannot add video input (codec ") +
                videoCodecName(settings.codec) + ")");
            return false;
        }
        [writer addInput:input];

        if (![writer startWriting]) {
            result_ = internal::videoOpenFailure(writer.error, "startWriting failed");
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
        // Bound (platform): encoder readiness waits at most 5 s (steady clock). Overflow rejects the frame and marks the take WriteFailed.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!pd->input.readyForMoreMediaData &&
               std::chrono::steady_clock::now() < deadline &&
               pd->writer.status == AVAssetWriterStatusWriting) {
            [NSThread sleepForTimeInterval:0.001];
        }
        if (pd->writer.status != AVAssetWriterStatusWriting) {
            const char* message = pd->writer.error.localizedDescription.UTF8String;
            result_ = SaveResult::fail(SaveError::WriteFailed,
                message ? message : "AVAssetWriter is not writing");
            pd->failed = true;
            return false;
        }
        if (!pd->input.readyForMoreMediaData) {
            result_ = SaveResult::fail(SaveError::WriteFailed, "input not ready (timeout)");
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
            result_ = SaveResult::fail(SaveError::WriteFailed, "could not create pixel buffer");
            pd->failed = true;
            return false;
        }

        // Bound (abi): CoreVideo must grant writable access to the native frame. Overflow drops the recorder frame and is counted.
        if (CVPixelBufferLockBaseAddress(pb, 0) != kCVReturnSuccess) {
            CVPixelBufferRelease(pb);
            return rejectFrameLayout();
        }
        uint8_t* dst = (uint8_t*)CVPixelBufferGetBaseAddress(pb);
        const auto layout = internal::recorderPixelBufferLayout(pb);
        // Bound (abi): CoreVideo BGRA8 extent, stride and capacity must cover the take. Overflow drops the recorder frame and is counted.
        if (!dst || !internal::recorderFrameFits(layout, pd->width, pd->height,
                                                internal::RecorderPixelFormat::BGRA8)) {
            CVPixelBufferUnlockBaseAddress(pb, 0);
            CVPixelBufferRelease(pb);
            return rejectFrameLayout();
        }
        const size_t dstStride = layout.rowBytes;
        const int w = pd->width;
        const int h = pd->height;
        for (int y = 0; y < h; ++y) {
            const unsigned char* srow = rgba + (size_t)y * w * 4;
            uint8_t* drow = dst + (size_t)y * dstStride;
            for (size_t x = 0; x < (size_t)w; ++x) {
                drow[x * 4 + 0] = srow[x * 4 + 2];  // B
                drow[x * 4 + 1] = srow[x * 4 + 1];  // G
                drow[x * 4 + 2] = srow[x * 4 + 0];  // R
                drow[x * 4 + 3] = srow[x * 4 + 3];  // A
            }
        }
        CVPixelBufferUnlockBaseAddress(pb, 0);

        CMTime t = CMTimeMakeWithSeconds(timeSec, 600);
        BOOL ok = pd->writer.status == AVAssetWriterStatusWriting && [pd->adaptor appendPixelBuffer:pb withPresentationTime:t];
        CVPixelBufferRelease(pb);

        if (!ok || pd->writer.status != AVAssetWriterStatusWriting) {
            const char* message = pd->writer.error.localizedDescription.UTF8String;
            result_ = SaveResult::fail(SaveError::WriteFailed,
                message ? message : "AVAssetWriter rejected append");
            pd->failed = true;
            return false;
        }
        ++pd->frameIndex;
        return true;
    }
}

SaveResult VideoWriter::closePlatform() {
    VideoWriterPlatformData* pd = platform_;
    if (!pd) return SaveResult::success();
    auto result = SaveResult::success();

    @autoreleasepool {
        if (pd->writer && pd->writer.status == AVAssetWriterStatusWriting) {
            [pd->input markAsFinished];
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [pd->writer finishWritingWithCompletionHandler:^{
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        }
        if (!pd->writer || pd->writer.status != AVAssetWriterStatusCompleted) {
            const char* message = pd->writer.error.localizedDescription.UTF8String;
            result = SaveResult::fail(SaveError::WriteFailed,
                message ? message : "AVAssetWriter did not complete");
        }
        pd->writer = nil;
        pd->input = nil;
        pd->adaptor = nil;
    }

    delete pd;
    platform_ = nullptr;
    return result;
}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
