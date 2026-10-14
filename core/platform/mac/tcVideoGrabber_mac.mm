// =============================================================================
// tcVideoGrabber_mac.mm - macOS AVFoundation 実装
// =============================================================================

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include "TrussC.h"

// Text of an Objective-C object (NSString, NSError, ...) for the Logger: its
// description as UTF-8, or "(null)" for nil (what %@ formatting printed).
static std::string tcLogText(id obj) {
    if (!obj) return "(null)";
    const char* text = [[obj description] UTF8String];
    return text ? std::string(text) : std::string();
}

#include <chrono>

namespace trussc {

// ---------------------------------------------------------------------------
// プラットフォーム固有データ構造
// ---------------------------------------------------------------------------
struct VideoGrabberPlatformData {
    AVCaptureSession* session = nil;
    AVCaptureDevice* device = nil;
    AVCaptureDeviceInput* input = nil;
    AVCaptureVideoDataOutput* output = nil;
    id<AVCaptureVideoDataOutputSampleBufferDelegate> delegate = nil;
    dispatch_queue_t captureQueue = nil;

    ~VideoGrabberPlatformData() { delete[] backBuffer; }

    // ダブルバッファリング用
    unsigned char* backBuffer = nullptr;
    std::atomic<bool> bufferReady{false};
    int bufferWidth = 0;
    int bufferHeight = 0;

    // サイズ変更通知用
    std::atomic<bool> needsResize{false};
    // Written by the capture thread before needsResize is set; read on main.
    std::atomic<int> newWidth{0};
    std::atomic<int> newHeight{0};
};

} // namespace trussc

// ---------------------------------------------------------------------------
// AVCaptureVideoDataOutputSampleBufferDelegate 実装
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
    auto* data = static_cast<trussc::VideoGrabberPlatformData*>(state->platformData.get());
    if (!data) return;

    // Stamp arrival time on the capture thread, BEFORE any processing.
    // This stays accurate even when the app's main loop stalls.
    uint64_t arrivalUs = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);

    // サイズが変わった場合はバッファをリサイズ（最初のフレームまたはサイズ変更時）
    if (width != data->bufferWidth || height != data->bufferHeight) {
        trussc::logVerbose("VideoGrabber") << "Frame size changed: " << width << " x " << height
            << " (was " << data->bufferWidth << " x "
            << data->bufferHeight << ")";

        // バックバッファをリサイズ
        if (data->backBuffer) {
            delete[] data->backBuffer;
        }
        data->backBuffer = new unsigned char[width * height * 4];
        data->bufferWidth = (int)width;
        data->bufferHeight = (int)height;

        // メインスレッド側にリサイズを通知（フラグで）
        data->newWidth.store((int)width, std::memory_order_relaxed);
        data->newHeight.store((int)height, std::memory_order_relaxed);
        data->needsResize.store(true);   // publishes the size above
    }

    if (baseAddress && data->backBuffer) {
        unsigned char* src = (unsigned char*)baseAddress;
        unsigned char* dst = data->backBuffer;

        // BGRA -> RGBA 変換してコピー
        for (size_t y = 0; y < height; y++) {
            unsigned char* srcRow = src + y * bytesPerRow;
            unsigned char* dstRow = dst + y * width * 4;
            for (size_t x = 0; x < width; x++) {
                size_t srcIdx = x * 4;
                size_t dstIdx = x * 4;
                dstRow[dstIdx + 0] = srcRow[srcIdx + 2];  // R <- B
                dstRow[dstIdx + 1] = srcRow[srcIdx + 1];  // G <- G
                dstRow[dstIdx + 2] = srcRow[srcIdx + 0];  // B <- R
                dstRow[dstIdx + 3] = 255;                  // A = 不透明
            }
        }

        // メインスレッド用バッファにコピー
        {
            // リサイズされてない場合のみコピー（リサイズはメインスレッドで処理）
            // Convert unlocked above; lock only for the copy the main thread reads.
            std::lock_guard<std::mutex> stateLock(state->mtx);
            if (state->pixels && !data->needsResize.load() && state->width == (int)width &&
                state->height == (int)height) {
                std::memcpy(state->pixels.get(), data->backBuffer, width * height * 4);
            }
        }

        state->dirty.store(true);

        // Timestamped frame queue (no-op unless enabled via setFrameQueueSize)
        state->frameQueue.push(data->backBuffer, (int)width, (int)height, arrivalUs);
    }

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

// Called when a frame is dropped - silently ignore to suppress OS warnings
- (void)captureOutput:(AVCaptureOutput*)output
  didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)connection {
    // Intentionally empty - frames are dropped when processing can't keep up
}

@end

namespace trussc {

// ---------------------------------------------------------------------------
// listDevicesPlatform - デバイス一覧を取得
// ---------------------------------------------------------------------------
std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    std::vector<VideoDeviceInfo> devices;

    @autoreleasepool {
        NSArray<AVCaptureDevice*>* avDevices = nil;

        // macOS 14.0+ では AVCaptureDeviceTypeExternal と AVCaptureDeviceTypeContinuityCamera を使用
        // Note: Need compile-time SDK check because these symbols don't exist in older SDKs
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
        if (@available(macOS 14.0, *)) {
            NSArray<AVCaptureDeviceType>* deviceTypes = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal,
                AVCaptureDeviceTypeContinuityCamera
            ];
            AVCaptureDeviceDiscoverySession* discoverySession =
                [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                       mediaType:AVMediaTypeVideo
                                                                        position:AVCaptureDevicePositionUnspecified];
            avDevices = discoverySession.devices;
        }
        else
#endif
        // macOS 10.15〜13.x では AVCaptureDeviceTypeExternalUnknown を使用
        if (@available(macOS 10.15, *)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSArray<AVCaptureDeviceType>* deviceTypes = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternalUnknown
            ];
            AVCaptureDeviceDiscoverySession* discoverySession =
                [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                       mediaType:AVMediaTypeVideo
                                                                        position:AVCaptureDevicePositionUnspecified];
            avDevices = discoverySession.devices;
#pragma clang diagnostic pop
        }
        // macOS 10.14 以前（フォールバック）
        else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            avDevices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
        }

        for (NSUInteger i = 0; i < avDevices.count; i++) {
            AVCaptureDevice* device = avDevices[i];
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
// setupPlatform - カメラを開始
// ---------------------------------------------------------------------------
bool VideoGrabber::setupPlatform() {
    @autoreleasepool {
        auto* data = new VideoGrabberPlatformData();
        state_->platformData = std::shared_ptr<VideoGrabberPlatformData>(data);

        // デバイスを取得
        NSArray<AVCaptureDevice*>* devices = nil;
        // macOS 14.0+ では AVCaptureDeviceTypeExternal と AVCaptureDeviceTypeContinuityCamera を使用
        // Note: Need compile-time SDK check because these symbols don't exist in older SDKs
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
        if (@available(macOS 14.0, *)) {
            NSArray<AVCaptureDeviceType>* deviceTypes = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal,
                AVCaptureDeviceTypeContinuityCamera
            ];
            AVCaptureDeviceDiscoverySession* discoverySession =
                [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                       mediaType:AVMediaTypeVideo
                                                                        position:AVCaptureDevicePositionUnspecified];
            devices = discoverySession.devices;
        }
        else
#endif
        // macOS 10.15〜13.x では AVCaptureDeviceTypeExternalUnknown を使用
        if (@available(macOS 10.15, *)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSArray<AVCaptureDeviceType>* deviceTypes = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternalUnknown
            ];
            AVCaptureDeviceDiscoverySession* discoverySession =
                [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                       mediaType:AVMediaTypeVideo
                                                                        position:AVCaptureDevicePositionUnspecified];
            devices = discoverySession.devices;
#pragma clang diagnostic pop
        }
        // macOS 10.14 以前（フォールバック）
        else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
        }

        if (deviceId_ >= (int)devices.count) {
            logError("VideoGrabber") << "Invalid device ID " << deviceId_ << " (only "
                << (unsigned long)devices.count << " devices available)";
            state_->platformData.reset();
            return false;
        }

        data->device = devices[deviceId_];

        // デバイス名を保存
        deviceName_ = data->device.localizedName.UTF8String;

        // =========================================================
        // デバイスのフォーマットから最適な解像度を選択（oF方式）
        // =========================================================
        NSError* configError = nil;
        [data->device lockForConfiguration:&configError];

        if (!configError) {
            float smallestDist = 99999999.0f;
            int bestW = 0, bestH = 0;
            AVCaptureDeviceFormat* bestFormat = nil;
            bool bestFpsOk = false;

            if (verbose_) {
                logVerbose("VideoGrabber") << "Searching for " << state_->requestedWidth << "x" << state_->requestedHeight
                                            << " in device formats...";
            }

            for (AVCaptureDeviceFormat* format in [data->device formats]) {
                CMFormatDescriptionRef desc = format.formatDescription;

                // ビデオフォーマットかチェック
                CMMediaType mediaType = CMFormatDescriptionGetMediaType(desc);
                if (mediaType != kCMMediaType_Video) continue;

                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(desc);
                int tw = dimensions.width;
                int th = dimensions.height;

                // Does this format support the desired frame rate? Same
                // resolution is often listed multiple times with different
                // fps ranges (e.g. 720p@60 and 720p@90), so fps must be part
                // of format selection, not just set afterwards.
                bool fpsOk = true;
                if (desiredFrameRate_ > 0) {
                    fpsOk = false;
                    for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
                        if (std::floor(range.minFrameRate) <= desiredFrameRate_ &&
                            std::ceil(range.maxFrameRate) >= desiredFrameRate_) {
                            fpsOk = true;
                            break;
                        }
                    }
                }

                // 完全一致（解像度＋fps）なら即採用
                if (tw == state_->requestedWidth && th == state_->requestedHeight && fpsOk) {
                    bestW = tw;
                    bestH = th;
                    bestFormat = format;
                    if (verbose_) {
                        logVerbose("VideoGrabber") << "Found exact match: " << tw << "x" << th;
                    }
                    break;
                }

                // ユークリッド距離で最も近いフォーマットを探す。
                // 解像度が主、fps対応はタイブレーク（解像度を勝手に落として
                // まで fps を優先しない）
                float dx = (float)(tw - state_->requestedWidth);
                float dy = (float)(th - state_->requestedHeight);
                float dist = sqrtf(dx * dx + dy * dy);

                if (dist < smallestDist ||
                    (dist == smallestDist && fpsOk && !bestFpsOk)) {
                    smallestDist = dist;
                    bestW = tw;
                    bestH = th;
                    bestFormat = format;
                    bestFpsOk = fpsOk;
                }
            }

            // 最適なフォーマットを設定
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
                // フォールバック: 現在のフォーマットを使用
                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(
                    data->device.activeFormat.formatDescription);
                state_->width = dimensions.width;
                state_->height = dimensions.height;
                if (verbose_) {
                    logVerbose("VideoGrabber") << "No suitable format found. Using device default: "
                                            << state_->width << "x" << state_->height;
                }
            }

            // フレームレート設定
            // AVFoundation はレンジ内の厳密な CMTime しか受け付けない
            // （例: BRIO の 60fps レンジは実際には 60.000240fps の離散値で、
            //  CMTimeMake(1, 60) はレンジ外として NSException を投げる）。
            // なので必ずレンジ自身の frameDuration を使う。
            if (desiredFrameRate_ > 0) {
                NSArray* supportedFrameRates = data->device.activeFormat.videoSupportedFrameRateRanges;
                AVFrameRateRange* bestRange = nil;
                float bestClamped = 0.0f;
                float bestDiff = 1e9f;

                for (AVFrameRateRange* range in supportedFrameRates) {
                    // clamp desired into this range, pick the range needing
                    // the least clamping
                    float clamped = (float)desiredFrameRate_;
                    if (clamped > (float)range.maxFrameRate) clamped = (float)range.maxFrameRate;
                    if (clamped < (float)range.minFrameRate) clamped = (float)range.minFrameRate;
                    float diff = fabsf(clamped - (float)desiredFrameRate_);
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        bestRange = range;
                        bestClamped = clamped;
                    }
                }

                if (bestRange) {
                    CMTime duration;
                    if ((float)desiredFrameRate_ >= (float)bestRange.maxFrameRate) {
                        duration = bestRange.minFrameDuration;   // fastest the range allows
                    } else if ((float)desiredFrameRate_ <= (float)bestRange.minFrameRate) {
                        duration = bestRange.maxFrameDuration;   // slowest the range allows
                    } else {
                        // strictly inside a continuous range - the exact value is valid
                        duration = CMTimeMake(1, desiredFrameRate_);
                    }
                    data->device.activeVideoMinFrameDuration = duration;
                    data->device.activeVideoMaxFrameDuration = duration;
                    if (bestDiff > 0.5f) {
                        logWarning("VideoGrabber") << "Requested frame rate " << desiredFrameRate_
                            << " not supported, using " << toString(bestClamped, 1) << " fps";
                    } else if (verbose_) {
                        logVerbose("VideoGrabber") << "Set frame rate to " << desiredFrameRate_ << " fps";
                    }
                }
            }

            [data->device unlockForConfiguration];
        } else {
            logWarning("VideoGrabber") << "Failed to lock device: " << tcLogText(configError.localizedDescription);
            // フォールバック: 現在のフォーマットを使用
            CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(
                data->device.activeFormat.formatDescription);
            state_->width = dimensions.width;
            state_->height = dimensions.height;
        }

        // セッションを作成
        data->session = [[AVCaptureSession alloc] init];

        // 入力を設定
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

        // 出力を設定（oF方式: 出力サイズも指定）
        data->output = [[AVCaptureVideoDataOutput alloc] init];
        data->output.videoSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(state_->width),
            (NSString*)kCVPixelBufferHeightKey: @(state_->height)
        };
        data->output.alwaysDiscardsLateVideoFrames = YES;

        // キャプチャキューを作成
        data->captureQueue = dispatch_queue_create("trussc.videograbber", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(data->captureQueue, data, data, nullptr);

        // デリゲートを設定
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

        if (verbose_) {
            logVerbose("VideoGrabber") << "Configured format: " << state_->width << "x" << state_->height;
        }

        // バックバッファを確保
        data->bufferWidth = state_->width;
        data->bufferHeight = state_->height;
        data->backBuffer = new unsigned char[state_->width * state_->height * 4];
        std::memset(data->backBuffer, 0, state_->width * state_->height * 4);

        // キャプチャ開始
        [data->session startRunning];

        logVerbose("VideoGrabber") << "Started capturing at " << state_->width << "x" << state_->height
            << " from " << tcLogText(data->device.localizedName);

        return true;
    }
}

// ---------------------------------------------------------------------------
// closePlatform - カメラを停止
// ---------------------------------------------------------------------------
static void stopCapture(const std::shared_ptr<internal::GrabberSharedState>& state) {
    @autoreleasepool {
        auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
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

        // ARC が処理するので明示的な release は不要
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
// updatePlatform - フレーム更新（メインスレッドで呼ばれる）
// ---------------------------------------------------------------------------
void VideoGrabber::updatePlatform() {
    // AVFoundation はバックグラウンドスレッドでフレームを取得するので
    // ここでは特に何もしない（state_->dirty のチェックは呼び出し元で行う）
}

// ---------------------------------------------------------------------------
// checkResizeNeeded - リサイズが必要かチェック
// ---------------------------------------------------------------------------
bool VideoGrabber::checkResizeNeeded() {
    if (!state_->platformData) return false;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    return data->needsResize.load();
}

// ---------------------------------------------------------------------------
// getNewSize - 新しいサイズを取得
// ---------------------------------------------------------------------------
void VideoGrabber::getNewSize(int& width, int& height) {
    if (!state_->platformData) {
        width = 0;
        height = 0;
        return;
    }
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    width = data->newWidth.load(std::memory_order_relaxed);
    height = data->newHeight.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// clearResizeFlag - リサイズフラグをクリア
// ---------------------------------------------------------------------------
void VideoGrabber::clearResizeFlag() {
    if (!state_->platformData) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    // Only once the handle has the size last reported: a size change that
    // arrived after getNewSize() keeps the flag for the next update().
    // state_->width / height are written on the main thread only.
    if (state_->width == data->newWidth.load(std::memory_order_relaxed) &&
        state_->height == data->newHeight.load(std::memory_order_relaxed))
        data->needsResize.store(false);
}

// ---------------------------------------------------------------------------
// updateDelegatePixels - ピクセルバッファ確保後にデリゲートを更新
// ---------------------------------------------------------------------------
void VideoGrabber::updateDelegatePixels() {
    // The delegate reads the State buffer while holding the State lock.
}

// ---------------------------------------------------------------------------
// checkCameraPermission - カメラ権限を確認
// ---------------------------------------------------------------------------
bool VideoGrabber::checkCameraPermission() {
    if (@available(macOS 10.14, *)) {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        return (status == AVAuthorizationStatusAuthorized);
    }
    return true;  // 10.14 より前はパーミッション不要
}

// ---------------------------------------------------------------------------
// requestCameraPermission - カメラ権限をリクエスト
// ---------------------------------------------------------------------------
void VideoGrabber::requestCameraPermission() {
    if (@available(macOS 10.14, *)) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
            if (granted) {
                trussc::logVerbose("VideoGrabber") << "Camera permission granted";
            } else {
                trussc::logWarning("VideoGrabber") << "Camera permission denied";
            }
        }];
    }
}

} // namespace trussc

#endif // __APPLE__
