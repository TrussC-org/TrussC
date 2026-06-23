// =============================================================================
// tcVideoRecorder_mac.mm - macOS AVFoundation (AVAssetWriter) implementation
// =============================================================================
// Implements the VideoWriter platform hooks declared in tc/video/tcVideoRecorder.h
//   - openPlatform()   : create the encoder (H.264 / HEVC / ProRes) + begin
//   - appendPlatform() : encode one RGBA8 (top-down) frame
//   - closePlatform()  : finalize and flush the file
// =============================================================================

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>

#include "TrussC.h"
#include <cmath>

namespace trussc {

// ---------------------------------------------------------------------------
// Platform state (ARC manages the strong ObjC members of this C++ struct)
// ---------------------------------------------------------------------------
struct VideoWriterPlatformData {
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* input = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    int width = 0;
    int height = 0;
    double fps = 30.0;
    int64_t frameIndex = 0;
    bool failed = false;
    CVPixelBufferRef pendingPB = NULL;   // locked buffer between lockFrame/submitFrame
};

// ---------------------------------------------------------------------------
// openPlatform - create the encoder and begin a session
// ---------------------------------------------------------------------------
bool VideoWriter::openPlatform(const std::string& fullPath, int w, int h,
                               float fps, const VideoRecordSettings& settings) {
    @autoreleasepool {
        // Map our codec to AVFoundation. ProRes lives in a QuickTime (.mov)
        // container; H.264/HEVC go in MPEG-4 (.mp4).
        NSString* codecKey = AVVideoCodecTypeH264;
        AVFileType fileType = AVFileTypeMPEG4;
        bool useBitrate = true;   // ProRes is quality-fixed (intra), ignores bitrate
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
        NSURL* url = [NSURL fileURLWithPath:nsPath];

        // Overwrite any existing file (AVAssetWriter refuses an existing path).
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
            // ~0.1 bits per pixel per second is a good default for clean demos.
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
            // Hardware encoders (HEVC / ProRes) read the frame from an IOSurface;
            // without this the pool yields CPU-only buffers and HW codecs produce
            // black/garbage (H.264's path happens to tolerate it).
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

// ---------------------------------------------------------------------------
// appendPlatform - encode one RGBA8 (top-down) frame
// ---------------------------------------------------------------------------
bool VideoWriter::appendPlatform(const unsigned char* rgba, double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->writer || pd->failed) return false;

    @autoreleasepool {
        // Offline writer: briefly wait for the input to accept more data.
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
            // Fallback if the pool is unavailable.
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
        // RGBA -> BGRA, row by row (respect destination stride/alignment).
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

        // Presentation time is decided by the caller (fixed-rate for manual
        // frames, real elapsed time for live capture). 600 = common timescale.
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

#if TC_ASYNC_SCREEN_CAPTURE
// ---------------------------------------------------------------------------
// lockFramePlatform / submitFramePlatform - zero-intermediate capture path.
// The recorder reads the GPU capture straight into the encoder's CVPixelBuffer
// (BGRA), so there is no temporary Pixels buffer to copy through. Called on the
// capture completion thread, serialized by the recorder's writer mutex.
// ---------------------------------------------------------------------------
unsigned char* VideoWriter::lockFramePlatform(int& strideOut) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->writer || pd->failed || pd->pendingPB) return nullptr;

    // Briefly wait for the encoder input to accept more data.
    int spins = 0;
    while (!pd->input.readyForMoreMediaData && spins < 5000) {
        [NSThread sleepForTimeInterval:0.001];
        ++spins;
    }
    if (!pd->input.readyForMoreMediaData) {
        logError("VideoWriter") << "input not ready (timeout)";
        pd->failed = true;
        return nullptr;
    }

    CVPixelBufferRef pb = NULL;
    if (pd->adaptor.pixelBufferPool) {
        CVPixelBufferPoolCreatePixelBuffer(NULL, pd->adaptor.pixelBufferPool, &pb);
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
        return nullptr;
    }

    CVPixelBufferLockBaseAddress(pb, 0);
    pd->pendingPB = pb;
    strideOut = (int)CVPixelBufferGetBytesPerRow(pb);
    return (unsigned char*)CVPixelBufferGetBaseAddress(pb);
}

bool VideoWriter::submitFramePlatform(double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->pendingPB) return false;

    CVPixelBufferRef pb = pd->pendingPB;
    pd->pendingPB = NULL;
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
#endif // TC_ASYNC_SCREEN_CAPTURE

// ---------------------------------------------------------------------------
// closePlatform - mark finished and flush the file synchronously
// ---------------------------------------------------------------------------
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

#endif // __APPLE__
