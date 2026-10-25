// =============================================================================
// iOS platform-specific functions
// =============================================================================

#include "TrussC.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS

#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreMotion/CoreMotion.h>
#import <CoreLocation/CoreLocation.h>
#import <AVFoundation/AVFoundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include "sokol_app_tc.h"

// ---------------------------------------------------------------------------
// Sensor managers (lazily initialized)
// ---------------------------------------------------------------------------
static CMMotionManager* _motionManager = nil;
static CLLocationManager* _locationManager = nil;
static bool _sensorsStarted = false;
static bool _locationStarted = false;
static trussc::Location _lastLocation;

// CLLocationManager delegate
@interface _TCSensorLocationDelegate : NSObject <CLLocationManagerDelegate>
@end

@implementation _TCSensorLocationDelegate
- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations {
    CLLocation* loc = locations.lastObject;
    if (loc) {
        _lastLocation.latitude = loc.coordinate.latitude;
        _lastLocation.longitude = loc.coordinate.longitude;
        _lastLocation.altitude = loc.altitude;
        _lastLocation.accuracy = (float)loc.horizontalAccuracy;
    }
}
- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error {
    trussc::logWarning() << "[Location] " << [[error localizedDescription] UTF8String];
}
- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
    if (manager.authorizationStatus == kCLAuthorizationStatusAuthorizedWhenInUse ||
        manager.authorizationStatus == kCLAuthorizationStatusAuthorizedAlways) {
        [manager startUpdatingLocation];
    }
}
@end

static _TCSensorLocationDelegate* _locationDelegate = nil;

static void _tcEnsureSensors() {
    if (_sensorsStarted) return;
    _sensorsStarted = true;

    _motionManager = [[CMMotionManager alloc] init];

    if (_motionManager.accelerometerAvailable) {
        _motionManager.accelerometerUpdateInterval = 1.0 / 60.0;
        [_motionManager startAccelerometerUpdates];
    }
    if (_motionManager.gyroAvailable) {
        _motionManager.gyroUpdateInterval = 1.0 / 60.0;
        [_motionManager startGyroUpdates];
    }
    if (_motionManager.deviceMotionAvailable) {
        _motionManager.deviceMotionUpdateInterval = 1.0 / 60.0;
        [_motionManager startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXMagneticNorthZVertical];
    }

    // Proximity
    [UIDevice currentDevice].proximityMonitoringEnabled = YES;

    // Battery
    [UIDevice currentDevice].batteryMonitoringEnabled = YES;
}

static void _tcEnsureLocation() {
    if (_locationStarted) return;
    _locationStarted = true;

    _locationDelegate = [[_TCSensorLocationDelegate alloc] init];
    _locationManager = [[CLLocationManager alloc] init];
    _locationManager.delegate = _locationDelegate;
    _locationManager.desiredAccuracy = kCLLocationAccuracyBest;
    [_locationManager requestWhenInUseAuthorization];
}

namespace trussc {

float getDisplayScaleFactor() {
    return (float)[UIScreen mainScreen].scale;
}

IVec2 internal::mainWindowPosition() {
    logWarning("Platform") << "getWindowPosition() is not supported on iOS";
    return IVec2(-1, -1);
}

void internal::setMainWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() is not supported on iOS";
    (void)x; (void)y;
}

void setWindowDecorated(bool decorated) {
    // No window decorations to toggle on iOS.
    (void)decorated;
}

void internal::setMainWindowSizeLogical(int width, int height) {
    // no-op on iOS (window size is fixed to screen size)
    (void)width;
    (void)height;
    logNotice("Platform") << "setWindowSize() is not supported on iOS; the window fills the screen";
}

fs::path getExecutablePath() {
    NSString* path = [[NSBundle mainBundle] executablePath];
    return path ? fs::path([path UTF8String]) : fs::path();
}

fs::path getExecutableDir() {
    fs::path exePath = getExecutablePath();
    if (exePath.empty()) return {};
    return exePath.parent_path();
}

// User data / temp folders: the app's own container folders (they belong to
// this app already, so no bundle id subfolder).
fs::path internal::platformUserDataRoot() {
    NSString* support = [NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES) firstObject];
    if (support) return fs::path(support.UTF8String);
    return fs::path(NSHomeDirectory().UTF8String) / "Library" / "Application Support";
}

fs::path internal::platformTempRoot() {
    return fs::path(NSTemporaryDirectory().UTF8String);
}

fs::path internal::platformAppBundlePath() {
    NSString* bundle = [[NSBundle mainBundle] bundlePath];
    if (!bundle) return {};
    return fs::path(bundle.UTF8String);
}

// ---------------------------------------------------------------------------
// Screenshot (Metal API)
// ---------------------------------------------------------------------------

std::pair<size_t, size_t> internal::captureWindowSize(const internal::WindowContext& ctx) {
    // Admission also runs in the next frame's prelude, before any drawable
    // exists. Query native framebuffer dimensions without acquiring or messaging
    // the borrowed drawable from a swapchain pass.
    if (!ctx.isMain) return {size_t(std::max(0, ctx.fbWidth)), size_t(std::max(0, ctx.fbHeight))};
    return {size_t(std::max(0, sapp_width())), size_t(std::max(0, sapp_height()))};
}

bool captureWindow(Pixels& outPixels) {
    if (!sg_isvalid()) return false;
    if (internal::hasPublicFboActivation()) return false;
    internal::CapturePrefixScope capturePhase;
    // Select only this native tick's rendered drawable. The host clears the
    // borrowed pointer at frame end; this job owns its texture until cleanup.
    const void* drawablePtr = internal::windowCaptureSource().drawable;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)drawablePtr;
    if (!drawable) {
        logError() << "[Screenshot] Failed to get Metal drawable";
        return false;
    }

    id<MTLTexture> srcTexture = drawable.texture;
    if (!srcTexture) {
        logError() << "[Screenshot] Failed to get Metal texture";
        return false;
    }

    NSUInteger width = srcTexture.width;
    NSUInteger height = srcTexture.height;
    mcp::internal::reconcileWindowCapture(width, height);
    MTLPixelFormat pixelFormat = srcTexture.pixelFormat;
    const auto layout = internal::recorderPackedLayout(width, height, internal::RecorderPixelFormat::RGBA8);
    // Bound (abi): Metal extent and format must fit the packed capture layout. Overflow drops the recorder frame and is counted.
    if (!layout.byteSize || width > (NSUInteger)std::numeric_limits<int>::max() ||
        height > (NSUInteger)std::numeric_limits<int>::max() ||
        (pixelFormat != MTLPixelFormatRGB10A2Unorm && pixelFormat != MTLPixelFormatBGRA8Unorm &&
         pixelFormat != MTLPixelFormatBGRA8Unorm_sRGB)) return false;

    // sokol sets framebufferOnly=YES on the CAMetalLayer, so reading the
    // drawable texture directly asserts under Metal validation. Blit into a
    // shared-storage staging texture, then read that (mirrors the mac path).
    id<MTLDevice> device = srcTexture.device;
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> stagingTexture = [device newTextureWithDescriptor:desc];
    if (!stagingTexture) {
        logError() << "[Screenshot] failed to create staging texture";
        return false;
    }

    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    if (!queue || queue.device != device) return false;
    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blitEnc = [cmdBuf blitCommandEncoder];
    [blitEnc copyFromTexture:srcTexture
                 sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:stagingTexture
            destinationSlice:0 destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blitEnc endEncoding];
    [cmdBuf commit];
    [cmdBuf waitUntilCompleted];
    if (cmdBuf.status != MTLCommandBufferStatusCompleted) return false;

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = layout.rowBytes;
    std::vector<uint8_t> rawData(layout.byteSize);
    [stagingTexture getBytes:rawData.data()
                 bytesPerRow:bytesPerRow
                  fromRegion:region
                 mipmapLevel:0];

    // Capture overwrites every byte; preserve a matching RGBA8 buffer.
    if (!outPixels.isAllocated() || outPixels.getWidth() != (int)width ||
        outPixels.getHeight() != (int)height || outPixels.getChannels() != 4 ||
        outPixels.getFormat() != PixelFormat::U8) {
        outPixels.allocate((int)width, (int)height, 4);
    }
    unsigned char* dst = outPixels.getData();
    // Bound (abi): pixel capacity must hold every captured row. Overflow drops the recorder frame and is counted.
    if (!dst || outPixels.getTotalBytes() < layout.byteSize) return false;

    if (pixelFormat == MTLPixelFormatRGB10A2Unorm) {
        // Modern iPhones use a 10-bit wide-color drawable. Unpack to RGBA8.
        // Layout: [A:2 (31-30)][B:10 (29-20)][G:10 (19-10)][R:10 (9-0)]
        const uint32_t* src = (const uint32_t*)rawData.data();
        for (NSUInteger i = 0; i < width * height; i++) {
            uint32_t pixel = src[i];
            dst[i * 4 + 0] = (uint8_t)(((pixel >>  0) & 0x3FF) >> 2);  // R
            dst[i * 4 + 1] = (uint8_t)(((pixel >> 10) & 0x3FF) >> 2);  // G
            dst[i * 4 + 2] = (uint8_t)(((pixel >> 20) & 0x3FF) >> 2);  // B
            dst[i * 4 + 3] = (uint8_t)(((pixel >> 30) & 0x3) * 85);    // A
        }
    } else {
        // BGRA8 fallback -> RGBA8
        memcpy(dst, rawData.data(), bytesPerRow * height);
        for (NSUInteger i = 0; i < width * height; i++) {
            unsigned char temp = dst[i * 4 + 0];
            dst[i * 4 + 0] = dst[i * 4 + 2];
            dst[i * 4 + 2] = temp;
        }
    }

    return true;
}

bool internal::captureWindowToFile(const std::filesystem::path& requestedPath) {
    const auto path = internal::resolveScreenshotPath(requestedPath);
    Pixels pixels;
    if (!captureWindow(pixels)) return false;
    return internal::saveScreenshotPixels(pixels, path);
}

bool internal::saveScreenshotPixels(const Pixels& pixels, const std::filesystem::path& requestedPath) {
    return writeScreenshotPixels(pixels, requestedPath).ok();
}

SaveResult internal::writeScreenshotPixels(const Pixels& pixels, const std::filesystem::path& requestedPath) {
    // Retire each encoded payload before its async admission slot is released.
    @autoreleasepool {
        const auto path = internal::resolveScreenshotPath(requestedPath);

        int width = pixels.getWidth();
        int height = pixels.getHeight();
        const auto ext = toLower(getFileExtension(path));
        if (ext == "bmp") {
            // Pixels uses stbi_write_bmp, as on the other native platforms.
            return internal::atomicWrite(path, [&](std::FILE* file) {
                return internal::writePixelsEncoded(file, pixels, ext, path);
            }, "Screenshot", true);
        }
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

        CGContextRef context = CGBitmapContextCreate(
            const_cast<unsigned char*>(pixels.getData()),
            width, height,
            8,
            width * 4,
            colorSpace,
            kCGImageAlphaPremultipliedLast | (CGBitmapInfo)kCGBitmapByteOrder32Big
        );

        if (!context) {
            CGColorSpaceRelease(colorSpace);
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "Failed to create CGContext"), "Screenshot");
        }

        CGImageRef cgImage = CGBitmapContextCreateImage(context);
        CGContextRelease(context);
        CGColorSpaceRelease(colorSpace);

        if (!cgImage) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "Failed to create CGImage"), "Screenshot");
        }

        // Use UIImage + PNG/JPEG representation
        UIImage* image = [UIImage imageWithCGImage:cgImage];
        CGImageRelease(cgImage);

        if (!image) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "Failed to create UIImage"), "Screenshot");
        }

        NSData* data = nil;
        if (ext == "jpg" || ext == "jpeg") {
            data = UIImageJPEGRepresentation(image, 0.9);
        } else {
            data = UIImagePNGRepresentation(image);
        }

        if (!data) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "Failed to create image data"), "Screenshot");
        }

        return internal::atomicWrite(path, data.bytes, data.length, "Screenshot", true);
    }
}

// ---------------------------------------------------------------------------
// App menu (macOS only) — stub
// ---------------------------------------------------------------------------
namespace internal {
void installAppMenu() {}
} // namespace internal

// ---------------------------------------------------------------------------
// System Volume
// ---------------------------------------------------------------------------
float getSystemVolume() {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    return session.outputVolume;
}

void setSystemVolume(float volume) {
    (void)volume;
    logWarning() << "[Platform] setSystemVolume() is not supported on iOS";
}

// ---------------------------------------------------------------------------
// Screen Brightness
// ---------------------------------------------------------------------------
float getSystemBrightness() {
    return (float)[UIScreen mainScreen].brightness;
}

void setSystemBrightness(float brightness) {
    [UIScreen mainScreen].brightness = (CGFloat)std::clamp(brightness, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Thermal
// ---------------------------------------------------------------------------
ThermalState getThermalState() {
    NSProcessInfoThermalState state = [NSProcessInfo processInfo].thermalState;
    switch (state) {
        case NSProcessInfoThermalStateNominal:  return ThermalState::Nominal;
        case NSProcessInfoThermalStateFair:     return ThermalState::Fair;
        case NSProcessInfoThermalStateSerious:  return ThermalState::Serious;
        case NSProcessInfoThermalStateCritical: return ThermalState::Critical;
        default: return ThermalState::Nominal;
    }
}

float getThermalTemperature() {
    return -1.0f; // iOS does not expose CPU temperature
}

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
float getBatteryLevel() {
    _tcEnsureSensors();
    float level = [UIDevice currentDevice].batteryLevel;
    return (level < 0) ? -1.0f : level;
}

bool isBatteryCharging() {
    _tcEnsureSensors();
    UIDeviceBatteryState state = [UIDevice currentDevice].batteryState;
    return (state == UIDeviceBatteryStateCharging || state == UIDeviceBatteryStateFull);
}

// ---------------------------------------------------------------------------
// Motion Sensors
// ---------------------------------------------------------------------------
Vec3 getAccelerometer() {
    _tcEnsureSensors();
    CMAccelerometerData* data = _motionManager.accelerometerData;
    if (!data) return Vec3(0, 0, 0);
    return Vec3((float)data.acceleration.x,
                (float)data.acceleration.y,
                (float)data.acceleration.z);
}

Vec3 getGyroscope() {
    _tcEnsureSensors();
    CMGyroData* data = _motionManager.gyroData;
    if (!data) return Vec3(0, 0, 0);
    return Vec3((float)data.rotationRate.x,
                (float)data.rotationRate.y,
                (float)data.rotationRate.z);
}

Quaternion getDeviceOrientation() {
    _tcEnsureSensors();
    CMDeviceMotion* motion = _motionManager.deviceMotion;
    if (!motion) return Quaternion(1, 0, 0, 0);
    CMQuaternion q = motion.attitude.quaternion;
    return Quaternion((float)q.w, (float)q.x, (float)q.y, (float)q.z);
}

float getCompassHeading() {
    _tcEnsureSensors();
    CMDeviceMotion* motion = _motionManager.deviceMotion;
    if (!motion) return 0.0f;
    // heading from magnetic north, convert degrees to radians
    double heading = motion.heading;
    return (float)(heading * TAU / 360.0);
}

// ---------------------------------------------------------------------------
// Proximity
// ---------------------------------------------------------------------------
bool isProximityClose() {
    _tcEnsureSensors();
    return [UIDevice currentDevice].proximityState;
}

// ---------------------------------------------------------------------------
// Location
// ---------------------------------------------------------------------------
Location getLocation() {
    _tcEnsureLocation();
    return _lastLocation;
}

// ---------------------------------------------------------------------------
// iOS immersive mode: hide status bar
} // namespace trussc (temporarily close for extern)
extern bool _sapp_ios_immersive_mode;
namespace trussc {
void setImmersiveMode(bool enabled) {
    _sapp_ios_immersive_mode = enabled;
    // Tell UIKit to re-query prefersStatusBarHidden
    UIWindow* window = UIApplication.sharedApplication.connectedScenes.allObjects.firstObject
        ? ((UIWindowScene*)UIApplication.sharedApplication.connectedScenes.allObjects.firstObject).keyWindow
        : nil;
    if (window.rootViewController) {
        [window.rootViewController setNeedsStatusBarAppearanceUpdate];
        [window.rootViewController setNeedsUpdateOfHomeIndicatorAutoHidden];
    }
}
bool getImmersiveMode() { return _sapp_ios_immersive_mode; }

// ---------------------------------------------------------------------------
// Keep screen on
// ---------------------------------------------------------------------------
static bool keepScreenOn_ = false;

void setKeepScreenOn(bool enabled) {
    keepScreenOn_ = enabled;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIApplication.sharedApplication.idleTimerDisabled = enabled ? YES : NO;
    });
}

bool getKeepScreenOn() { return keepScreenOn_; }

// ---------------------------------------------------------------------------
// Orientation
// ---------------------------------------------------------------------------
void setOrientation(Orientation mask) {
    sapp_ios_set_supported_orientations(static_cast<uint32_t>(mask));
}

void bringWindowToFront() {
    // no-op: iOS apps are always foreground when running
}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
