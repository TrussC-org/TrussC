// =============================================================================
// iOS VideoGrabber implementation (AVFoundation)
// Based on macOS version with iOS-specific adjustments:
// - Device types: BuiltInWideAngleCamera (front + back)
// - Video orientation handling for device rotation
// - Front camera mirroring
// - Background/foreground transition handling
// =============================================================================

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <UIKit/UIKit.h>

#include "TrussC.h"

namespace trussc {

// ---------------------------------------------------------------------------
// Platform-specific data
// ---------------------------------------------------------------------------
struct VideoGrabberPlatformData {
    AVCaptureSession* session = nil;
    AVCaptureDevice* device = nil;
    AVCaptureDeviceInput* input = nil;
    AVCaptureVideoDataOutput* output = nil;
    id<AVCaptureVideoDataOutputSampleBufferDelegate> delegate = nil;
    dispatch_queue_t captureQueue = nil;

    // Double buffering
    unsigned char* backBuffer = nullptr;
    std::atomic<bool> bufferReady{false};
    int bufferWidth = 0;
    int bufferHeight = 0;

    // Resize notification
    std::atomic<bool> needsResize{false};
    int newWidth = 0;
    int newHeight = 0;

    // Background observer tokens
    id<NSObject> bgObserver = nil;
    id<NSObject> fgObserver = nil;

    // Orientation tracking
    UIInterfaceOrientation lastOrientation = UIInterfaceOrientationUnknown;
};

} // namespace trussc

// ---------------------------------------------------------------------------
// AVCaptureVideoDataOutputSampleBufferDelegate
// ---------------------------------------------------------------------------
@interface TrussCVideoGrabberDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) trussc::VideoGrabberPlatformData* platformData;
@property (nonatomic, assign) unsigned char* targetPixels;
@property (nonatomic, assign) std::atomic<bool>* pixelsDirty;
@property (nonatomic, assign) std::mutex* mutex;
@end

@implementation TrussCVideoGrabberDelegate

- (void)captureOutput:(AVCaptureOutput*)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)connection {
    if (!_platformData || !_targetPixels || !_pixelsDirty || !_mutex) return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);

    // Handle size change (first frame or format change)
    if (width != _platformData->bufferWidth || height != _platformData->bufferHeight) {
        NSLog(@"VideoGrabber: Frame size changed: %zu x %zu (was %d x %d)",
              width, height, _platformData->bufferWidth, _platformData->bufferHeight);

        if (_platformData->backBuffer) {
            delete[] _platformData->backBuffer;
        }
        _platformData->backBuffer = new unsigned char[width * height * 4];
        _platformData->bufferWidth = (int)width;
        _platformData->bufferHeight = (int)height;

        // Notify main thread
        _platformData->needsResize.store(true);
        _platformData->newWidth = (int)width;
        _platformData->newHeight = (int)height;
    }

    if (baseAddress && _platformData->backBuffer) {
        unsigned char* src = (unsigned char*)baseAddress;
        unsigned char* dst = _platformData->backBuffer;

        // BGRA -> RGBA conversion
        for (size_t y = 0; y < height; y++) {
            unsigned char* srcRow = src + y * bytesPerRow;
            unsigned char* dstRow = dst + y * width * 4;
            for (size_t x = 0; x < width; x++) {
                size_t srcIdx = x * 4;
                size_t dstIdx = x * 4;
                dstRow[dstIdx + 0] = srcRow[srcIdx + 2];  // R <- B
                dstRow[dstIdx + 1] = srcRow[srcIdx + 1];  // G <- G
                dstRow[dstIdx + 2] = srcRow[srcIdx + 0];  // B <- R
                dstRow[dstIdx + 3] = 255;                  // A = opaque
            }
        }

        // Copy to main thread buffer
        {
            std::lock_guard<std::mutex> lock(*_mutex);
            if (!_platformData->needsResize.load()) {
                std::memcpy(_targetPixels, _platformData->backBuffer, width * height * 4);
            }
        }

        _pixelsDirty->store(true);
    }

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

// Suppress OS warnings for dropped frames
- (void)captureOutput:(AVCaptureOutput*)output
  didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)connection {
}

@end

namespace trussc {

// ---------------------------------------------------------------------------
// listDevicesPlatform - enumerate cameras
// ---------------------------------------------------------------------------
std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    std::vector<VideoDeviceInfo> devices;

    @autoreleasepool {
        NSArray<AVCaptureDeviceType>* deviceTypes = @[
            AVCaptureDeviceTypeBuiltInWideAngleCamera
        ];
        AVCaptureDeviceDiscoverySession* discoverySession =
            [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                   mediaType:AVMediaTypeVideo
                                                                    position:AVCaptureDevicePositionUnspecified];

        for (NSUInteger i = 0; i < discoverySession.devices.count; i++) {
            AVCaptureDevice* device = discoverySession.devices[i];
            VideoDeviceInfo info;
            info.deviceId = (int)i;
            info.deviceName = device.localizedName.UTF8String;
            info.uniqueId = device.uniqueID.UTF8String;
            devices.push_back(info);
        }
    }

    return devices;
}

// ---------------------------------------------------------------------------
// setupPlatform - start camera
// ---------------------------------------------------------------------------
bool VideoGrabber::setupPlatform() {
    @autoreleasepool {
        auto* data = new VideoGrabberPlatformData();
        platformHandle_ = data;

        // Discover devices
        NSArray<AVCaptureDeviceType>* deviceTypes = @[
            AVCaptureDeviceTypeBuiltInWideAngleCamera
        ];
        AVCaptureDeviceDiscoverySession* discoverySession =
            [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                   mediaType:AVMediaTypeVideo
                                                                    position:AVCaptureDevicePositionUnspecified];
        NSArray<AVCaptureDevice*>* devices = discoverySession.devices;

        if (deviceId_ >= (int)devices.count) {
            NSLog(@"VideoGrabber: Invalid device ID %d (only %lu devices available)",
                  deviceId_, (unsigned long)devices.count);
            delete data;
            platformHandle_ = nullptr;
            return false;
        }

        data->device = devices[deviceId_];
        deviceName_ = data->device.localizedName.UTF8String;

        // ==========================================================
        // Select best format matching requested resolution
        // ==========================================================
        NSError* configError = nil;
        [data->device lockForConfiguration:&configError];

        if (!configError) {
            float smallestDist = 99999999.0f;
            int bestW = 0, bestH = 0;
            AVCaptureDeviceFormat* bestFormat = nil;

            if (verbose_) {
                NSLog(@"VideoGrabber: Searching for %dx%d in device formats...",
                      requestedWidth_, requestedHeight_);
            }

            for (AVCaptureDeviceFormat* format in [data->device formats]) {
                CMFormatDescriptionRef desc = format.formatDescription;
                CMMediaType mediaType = CMFormatDescriptionGetMediaType(desc);
                if (mediaType != kCMMediaType_Video) continue;

                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(desc);
                int tw = dimensions.width;
                int th = dimensions.height;

                if (tw == requestedWidth_ && th == requestedHeight_) {
                    bestW = tw;
                    bestH = th;
                    bestFormat = format;
                    if (verbose_) {
                        NSLog(@"VideoGrabber: Found exact match: %dx%d", tw, th);
                    }
                    break;
                }

                float dx = (float)(tw - requestedWidth_);
                float dy = (float)(th - requestedHeight_);
                float dist = sqrtf(dx * dx + dy * dy);

                if (dist < smallestDist) {
                    smallestDist = dist;
                    bestW = tw;
                    bestH = th;
                    bestFormat = format;
                }
            }

            if (bestFormat != nil && bestW > 0 && bestH > 0) {
                if (bestW != requestedWidth_ || bestH != requestedHeight_) {
                    if (verbose_) {
                        NSLog(@"VideoGrabber: Requested %dx%d not available. Using closest: %dx%d",
                              requestedWidth_, requestedHeight_, bestW, bestH);
                    }
                }
                [data->device setActiveFormat:bestFormat];
                width_ = bestW;
                height_ = bestH;
            } else {
                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(
                    data->device.activeFormat.formatDescription);
                width_ = dimensions.width;
                height_ = dimensions.height;
                if (verbose_) {
                    NSLog(@"VideoGrabber: No suitable format found. Using device default: %dx%d",
                          width_, height_);
                }
            }

            // Frame rate
            if (desiredFrameRate_ > 0) {
                AVFrameRateRange* desiredRange = nil;
                NSArray* supportedFrameRates = data->device.activeFormat.videoSupportedFrameRateRanges;

                for (AVFrameRateRange* range in supportedFrameRates) {
                    if (std::floor(range.minFrameRate) <= desiredFrameRate_ &&
                        std::ceil(range.maxFrameRate) >= desiredFrameRate_) {
                        desiredRange = range;
                        break;
                    }
                }

                if (desiredRange) {
                    data->device.activeVideoMinFrameDuration = CMTimeMake(1, desiredFrameRate_);
                    data->device.activeVideoMaxFrameDuration = CMTimeMake(1, desiredFrameRate_);
                    if (verbose_) {
                        NSLog(@"VideoGrabber: Set frame rate to %d fps", desiredFrameRate_);
                    }
                } else if (verbose_) {
                    NSLog(@"VideoGrabber: Requested frame rate %d not supported", desiredFrameRate_);
                }
            }

            [data->device unlockForConfiguration];
        } else {
            NSLog(@"VideoGrabber: Failed to lock device: %@", configError.localizedDescription);
            CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(
                data->device.activeFormat.formatDescription);
            width_ = dimensions.width;
            height_ = dimensions.height;
        }

        // Create session
        data->session = [[AVCaptureSession alloc] init];

        // Input
        NSError* error = nil;
        data->input = [AVCaptureDeviceInput deviceInputWithDevice:data->device error:&error];
        if (error || !data->input) {
            NSLog(@"VideoGrabber: Failed to create input: %@", error.localizedDescription);
            delete data;
            platformHandle_ = nullptr;
            return false;
        }

        if (![data->session canAddInput:data->input]) {
            NSLog(@"VideoGrabber: Cannot add input to session");
            delete data;
            platformHandle_ = nullptr;
            return false;
        }
        [data->session addInput:data->input];

        // Output (BGRA format, no explicit size on iOS - controlled by setActiveFormat)
        data->output = [[AVCaptureVideoDataOutput alloc] init];
        data->output.videoSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        data->output.alwaysDiscardsLateVideoFrames = YES;

        // Capture queue
        data->captureQueue = dispatch_queue_create("trussc.videograbber", DISPATCH_QUEUE_SERIAL);

        // Delegate
        TrussCVideoGrabberDelegate* delegate = [[TrussCVideoGrabberDelegate alloc] init];
        delegate.platformData = data;
        delegate.targetPixels = pixels_;
        delegate.pixelsDirty = &pixelsDirty_;
        delegate.mutex = &mutex_;
        data->delegate = delegate;

        [data->output setSampleBufferDelegate:delegate queue:data->captureQueue];

        if (![data->session canAddOutput:data->output]) {
            NSLog(@"VideoGrabber: Cannot add output to session");
            delete data;
            platformHandle_ = nullptr;
            return false;
        }
        [data->session addOutput:data->output];

        // Set video mirroring for front camera
        AVCaptureConnection* connection = [data->output connectionWithMediaType:AVMediaTypeVideo];
        if (connection) {
            if (data->device.position == AVCaptureDevicePositionFront &&
                connection.isVideoMirroringSupported) {
                connection.automaticallyAdjustsVideoMirroring = NO;
                connection.videoMirrored = YES;
            }
        }

        if (verbose_) {
            NSLog(@"VideoGrabber: Configured format: %dx%d", width_, height_);
        }

        // Back buffer
        data->bufferWidth = width_;
        data->bufferHeight = height_;
        data->backBuffer = new unsigned char[width_ * height_ * 4];
        std::memset(data->backBuffer, 0, width_ * height_ * 4);

        // Background/foreground transition observers
        data->bgObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIApplicationDidEnterBackgroundNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
            if (data->session.isRunning) {
                [data->session stopRunning];
                NSLog(@"VideoGrabber: Paused (app entered background)");
            }
        }];

        data->fgObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIApplicationWillEnterForegroundNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
            if (!data->session.isRunning) {
                [data->session startRunning];
                NSLog(@"VideoGrabber: Resumed (app entered foreground)");
            }
        }];

        // Start capture
        [data->session startRunning];

        NSLog(@"VideoGrabber: Started capturing at %dx%d from %@",
              width_, height_, data->device.localizedName);

        return true;
    }
}

// ---------------------------------------------------------------------------
// closePlatform - stop camera
// ---------------------------------------------------------------------------
void VideoGrabber::closePlatform() {
    if (!platformHandle_) return;

    @autoreleasepool {
        auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);

        // Remove background/foreground observers
        if (data->bgObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:data->bgObserver];
            data->bgObserver = nil;
        }
        if (data->fgObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:data->fgObserver];
            data->fgObserver = nil;
        }

        if (data->session) {
            [data->session stopRunning];
            [data->session removeInput:data->input];
            [data->session removeOutput:data->output];
        }

        if (data->backBuffer) {
            delete[] data->backBuffer;
            data->backBuffer = nullptr;
        }

        data->session = nil;
        data->device = nil;
        data->input = nil;
        data->output = nil;
        data->delegate = nil;
        data->captureQueue = nil;

        delete data;
        platformHandle_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// updatePlatform - per-frame update (called on main thread)
// ---------------------------------------------------------------------------
void VideoGrabber::updatePlatform() {
    if (!platformHandle_) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);

    // Update video orientation to match device orientation
    UIInterfaceOrientation orientation = UIInterfaceOrientationPortrait;
    UIWindowScene* scene = (UIWindowScene*)UIApplication.sharedApplication.connectedScenes.allObjects.firstObject;
    if (scene) {
        orientation = scene.interfaceOrientation;
    }

    if (orientation != data->lastOrientation) {
        data->lastOrientation = orientation;

        AVCaptureConnection* connection = [data->output connectionWithMediaType:AVMediaTypeVideo];
        if (!connection) return;

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 170000
        if (@available(iOS 17.0, *)) {
            CGFloat angle = 90;  // Portrait
            switch (orientation) {
                case UIInterfaceOrientationPortrait:            angle = 90;  break;
                case UIInterfaceOrientationPortraitUpsideDown:  angle = 270; break;
                case UIInterfaceOrientationLandscapeRight:      angle = 0;   break;
                case UIInterfaceOrientationLandscapeLeft:       angle = 180; break;
                default: break;
            }
            connection.videoRotationAngle = angle;
        } else
#endif
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            if (connection.isVideoOrientationSupported) {
                AVCaptureVideoOrientation videoOri;
                switch (orientation) {
                    case UIInterfaceOrientationPortrait:
                        videoOri = AVCaptureVideoOrientationPortrait; break;
                    case UIInterfaceOrientationPortraitUpsideDown:
                        videoOri = AVCaptureVideoOrientationPortraitUpsideDown; break;
                    case UIInterfaceOrientationLandscapeRight:
                        videoOri = AVCaptureVideoOrientationLandscapeRight; break;
                    case UIInterfaceOrientationLandscapeLeft:
                        videoOri = AVCaptureVideoOrientationLandscapeLeft; break;
                    default:
                        videoOri = AVCaptureVideoOrientationPortrait; break;
                }
                connection.videoOrientation = videoOri;
            }
#pragma clang diagnostic pop
        }
    }
}

// ---------------------------------------------------------------------------
// Resize notification methods
// ---------------------------------------------------------------------------
bool VideoGrabber::checkResizeNeeded() {
    if (!platformHandle_) return false;
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    return data->needsResize.load();
}

void VideoGrabber::getNewSize(int& width, int& height) {
    if (!platformHandle_) {
        width = 0;
        height = 0;
        return;
    }
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    width = data->newWidth;
    height = data->newHeight;
}

void VideoGrabber::clearResizeFlag() {
    if (!platformHandle_) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    data->needsResize.store(false);
}

// ---------------------------------------------------------------------------
// updateDelegatePixels - update pixel buffer pointer in delegate
// ---------------------------------------------------------------------------
void VideoGrabber::updateDelegatePixels() {
    if (!platformHandle_) return;

    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    if (data->delegate) {
        TrussCVideoGrabberDelegate* delegate = (TrussCVideoGrabberDelegate*)data->delegate;
        delegate.targetPixels = pixels_;
        if (verbose_) {
            NSLog(@"VideoGrabber: Updated delegate pixels pointer to %p", pixels_);
        }
    }
}

// ---------------------------------------------------------------------------
// Camera permission
// ---------------------------------------------------------------------------
bool VideoGrabber::checkCameraPermission() {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    return (status == AVAuthorizationStatusAuthorized);
}

void VideoGrabber::requestCameraPermission() {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL granted) {
        if (granted) {
            NSLog(@"VideoGrabber: Camera permission granted");
        } else {
            NSLog(@"VideoGrabber: Camera permission denied");
        }
    }];
}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
