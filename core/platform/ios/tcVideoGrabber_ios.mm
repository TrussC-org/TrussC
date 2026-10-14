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

// Text of an Objective-C object (NSString, NSError, ...) for the Logger: its
// description as UTF-8, or "(null)" for nil (what %@ formatting printed).
static std::string tcLogText(id obj) {
    if (!obj) return "(null)";
    const char* text = [[obj description] UTF8String];
    return text ? std::string(text) : std::string();
}

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
    std::recursive_mutex sessionControl;

    ~VideoGrabberPlatformData() { delete[] backBuffer; }

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
@interface TrussCVideoGrabberDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    std::weak_ptr<trussc::internal::GrabberSharedState> captureState;
}
@end

@implementation TrussCVideoGrabberDelegate

- (void)captureOutput:(AVCaptureOutput*)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)connection {
    auto state = captureState.lock();
    if (!state || state->stopped.load()) return;
    std::lock_guard<std::mutex> stateLock(state->mtx);
    if (!state->pixels) return;
    auto* data = static_cast<trussc::VideoGrabberPlatformData*>(state->platformData.get());
    if (!data) return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);

    // Handle size change (first frame or format change)
    if (width != data->bufferWidth || height != data->bufferHeight) {
        trussc::logVerbose("VideoGrabber") << "Frame size changed: " << width << " x " << height
            << " (was " << data->bufferWidth << " x "
            << data->bufferHeight << ")";

        if (data->backBuffer) {
            delete[] data->backBuffer;
        }
        data->backBuffer = new unsigned char[width * height * 4];
        data->bufferWidth = (int)width;
        data->bufferHeight = (int)height;

        // Notify main thread
        data->needsResize.store(true);
        data->newWidth = (int)width;
        data->newHeight = (int)height;
    }

    if (baseAddress && data->backBuffer) {
        unsigned char* src = (unsigned char*)baseAddress;
        unsigned char* dst = data->backBuffer;

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
            if (!data->needsResize.load() && state->width == (int)width &&
                state->height == (int)height) {
                std::memcpy(state->pixels.get(), data->backBuffer, width * height * 4);
            }
        }

        state->dirty.store(true);
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
        state_->platformData = std::shared_ptr<VideoGrabberPlatformData>(data);

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
            logError("VideoGrabber") << "Invalid device ID " << deviceId_ << " (only "
                << (unsigned long)devices.count << " devices available)";
            state_->platformData.reset();
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
                logVerbose("VideoGrabber") << "Searching for " << state_->requestedWidth << "x"
                    << state_->requestedHeight << " in device formats...";
            }

            for (AVCaptureDeviceFormat* format in [data->device formats]) {
                CMFormatDescriptionRef desc = format.formatDescription;
                CMMediaType mediaType = CMFormatDescriptionGetMediaType(desc);
                if (mediaType != kCMMediaType_Video) continue;

                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(desc);
                int tw = dimensions.width;
                int th = dimensions.height;

                if (tw == state_->requestedWidth && th == state_->requestedHeight) {
                    bestW = tw;
                    bestH = th;
                    bestFormat = format;
                    if (verbose_) {
                        logVerbose("VideoGrabber") << "Found exact match: " << tw << "x" << th;
                    }
                    break;
                }

                float dx = (float)(tw - state_->requestedWidth);
                float dy = (float)(th - state_->requestedHeight);
                float dist = sqrtf(dx * dx + dy * dy);

                if (dist < smallestDist) {
                    smallestDist = dist;
                    bestW = tw;
                    bestH = th;
                    bestFormat = format;
                }
            }

            if (bestFormat != nil && bestW > 0 && bestH > 0) {
                if (bestW != state_->requestedWidth || bestH != state_->requestedHeight) {
                    if (verbose_) {
                        logVerbose("VideoGrabber") << "Requested " << state_->requestedWidth << "x" << state_->requestedHeight
                            << " not available. Using closest: " << bestW << "x" << bestH;
                    }
                }
                [data->device setActiveFormat:bestFormat];
                state_->width = bestW;
                state_->height = bestH;
            } else {
                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(
                    data->device.activeFormat.formatDescription);
                state_->width = dimensions.width;
                state_->height = dimensions.height;
                if (verbose_) {
                    logVerbose("VideoGrabber") << "No suitable format found. Using device default: "
                        << state_->width << "x" << state_->height;
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
                        logVerbose("VideoGrabber") << "Set frame rate to " << desiredFrameRate_ << " fps";
                    }
                } else if (verbose_) {
                    logWarning("VideoGrabber") << "Requested frame rate " << desiredFrameRate_ << " not supported";
                }
            }

            [data->device unlockForConfiguration];
        } else {
            logWarning("VideoGrabber") << "Failed to lock device: " << tcLogText(configError.localizedDescription);
            CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(
                data->device.activeFormat.formatDescription);
            state_->width = dimensions.width;
            state_->height = dimensions.height;
        }

        // Create session
        data->session = [[AVCaptureSession alloc] init];

        // Input
        NSError* error = nil;
        data->input = [AVCaptureDeviceInput deviceInputWithDevice:data->device error:&error];
        if (error || !data->input) {
            logError("VideoGrabber") << "Failed to create input: " << tcLogText(error.localizedDescription);
            state_->platformData.reset();
            return false;
        }

        if (![data->session canAddInput:data->input]) {
            logError("VideoGrabber") << "Cannot add input to session";
            state_->platformData.reset();
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
        dispatch_queue_set_specific(data->captureQueue, data, data, nullptr);

        // Delegate
        TrussCVideoGrabberDelegate* delegate = [[TrussCVideoGrabberDelegate alloc] init];
        delegate->captureState = state_;
        data->delegate = delegate;

        [data->output setSampleBufferDelegate:delegate queue:data->captureQueue];

        if (![data->session canAddOutput:data->output]) {
            logError("VideoGrabber") << "Cannot add output to session";
            state_->platformData.reset();
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
            logVerbose("VideoGrabber") << "Configured format: " << state_->width << "x" << state_->height;
        }

        // Back buffer
        data->bufferWidth = state_->width;
        data->bufferHeight = state_->height;
        data->backBuffer = new unsigned char[state_->width * state_->height * 4];
        std::memset(data->backBuffer, 0, state_->width * state_->height * 4);

        std::weak_ptr<internal::GrabberSharedState> observerState = state_;
        // Background/foreground transition observers
        data->bgObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIApplicationDidEnterBackgroundNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
            auto state = observerState.lock();
            if (!state || state->stopped.load()) return;
            auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
            std::lock_guard<std::recursive_mutex> lock(data->sessionControl);
            if (state->stopped.load()) return;
            if (data->session.isRunning) {
                [data->session stopRunning];
                trussc::logVerbose("VideoGrabber") << "Paused (app entered background)";
            }
        }];

        data->fgObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIApplicationWillEnterForegroundNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
            auto state = observerState.lock();
            if (!state || state->stopped.load()) return;
            auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
            std::lock_guard<std::recursive_mutex> lock(data->sessionControl);
            if (state->stopped.load()) return;
            if (!data->session.isRunning) {
                [data->session startRunning];
                trussc::logVerbose("VideoGrabber") << "Resumed (app entered foreground)";
            }
        }];

        // Start capture
        [data->session startRunning];

        logVerbose("VideoGrabber") << "Started capturing at " << state_->width << "x" << state_->height
            << " from " << tcLogText(data->device.localizedName);

        return true;
    }
}

// ---------------------------------------------------------------------------
// closePlatform - stop camera
// ---------------------------------------------------------------------------
static void stopCapture(const std::shared_ptr<internal::GrabberSharedState>& state) {
    @autoreleasepool {
        auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
        // Remove background/foreground observers
        if (data->bgObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:data->bgObserver];
            data->bgObserver = nil;
        }
        if (data->fgObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:data->fgObserver];
            data->fgObserver = nil;
        }

        // Serialize in-flight notification callbacks with session release.
        // Recursive for close invoked by a notification's final log callback.
        std::lock_guard<std::recursive_mutex> sessionLock(data->sessionControl);
        if (data->session) {
            [data->session stopRunning];
        }

        // Stop delivery and wait for queued capture callbacks before freeing
        // the platform data, pixel buffers or the grabber's shared state.
        [data->output setSampleBufferDelegate:nil queue:NULL];
        if (data->captureQueue) {
            dispatch_sync(data->captureQueue, ^{});
        }

        if (data->session) {
            [data->session removeInput:data->input];
            [data->session removeOutput:data->output];
        }

        data->session = nil;
        data->device = nil;
        data->input = nil;
        data->output = nil;
        data->delegate = nil;
        data->captureQueue = nil;

    }
}

void VideoGrabber::closePlatform() {
    if (!state_->platformData) return;
    auto state = state_;
    auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
    if (dispatch_get_specific(data) == data) {
        // Session stop/drain must not wait for the callback executing it.
        // This closure owns the old State until unregister and drain finish.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
            stopCapture(state);
        });
        return;
    }
    stopCapture(state);
}

// ---------------------------------------------------------------------------
// updatePlatform - per-frame update (called on main thread)
// ---------------------------------------------------------------------------
void VideoGrabber::updatePlatform() {
    if (!state_->platformData) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());

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
    if (!state_->platformData) return false;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    return data->needsResize.load();
}

void VideoGrabber::getNewSize(int& width, int& height) {
    if (!state_->platformData) {
        width = 0;
        height = 0;
        return;
    }
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    std::lock_guard<std::mutex> lock(state_->mtx);
    width = data->newWidth;
    height = data->newHeight;
}

void VideoGrabber::clearResizeFlag() {
    if (!state_->platformData) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    std::lock_guard<std::mutex> lock(state_->mtx);
    if (state_->width == data->newWidth && state_->height == data->newHeight)
        data->needsResize.store(false);
}

// ---------------------------------------------------------------------------
// updateDelegatePixels - update pixel buffer pointer in delegate
// ---------------------------------------------------------------------------
void VideoGrabber::updateDelegatePixels() {
    // The delegate reads the State buffer while holding the State lock.
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
            trussc::logVerbose("VideoGrabber") << "Camera permission granted";
        } else {
            trussc::logWarning("VideoGrabber") << "Camera permission denied";
        }
    }];
}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
