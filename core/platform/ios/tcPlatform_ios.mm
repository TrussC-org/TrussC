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

#include "sokol_app.h"

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

IVec2 getWindowPosition() {
    logWarning("Platform") << "getWindowPosition() is not supported on iOS";
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() is not supported on iOS";
    (void)x; (void)y;
}

void setWindowDecorated(bool decorated) {
    // No window decorations to toggle on iOS.
    (void)decorated;
}

void setWindowSizeLogical(int width, int height) {
    // no-op on iOS (window size is fixed to screen size)
    (void)width;
    (void)height;
}

std::string getExecutablePath() {
    NSString* path = [[NSBundle mainBundle] executablePath];
    return std::string([path UTF8String]);
}

std::string getExecutableDir() {
    NSString* path = [[NSBundle mainBundle] executablePath];
    NSString* dir = [path stringByDeletingLastPathComponent];
    return std::string([dir UTF8String]) + "/";
}

// ---------------------------------------------------------------------------
// Screenshot (Metal API)
// ---------------------------------------------------------------------------

bool captureWindow(Pixels& outPixels) {
    // Read back the drawable this frame ACTUALLY rendered into (recorded by the
    // swapchain pass). NOT sapp_get_swapchain() — that advances to the next,
    // unrendered drawable, scrambling captures on GPU-heavy frames. Fall back
    // only if no pass ran yet. (Mirrors the macOS fix.)
    const void* drawablePtr = internal::lastSwapchainDrawable;
    if (!drawablePtr) drawablePtr = sapp_get_swapchain().metal.current_drawable;
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
    MTLPixelFormat pixelFormat = srcTexture.pixelFormat;

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

    static id<MTLCommandQueue> s_blitQueue = nil;
    if (!s_blitQueue || s_blitQueue.device != device) {
        s_blitQueue = [device newCommandQueue];
    }
    id<MTLCommandBuffer>      cmdBuf  = [s_blitQueue commandBuffer];
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

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * 4;
    std::vector<uint8_t> rawData(bytesPerRow * height);
    [stagingTexture getBytes:rawData.data()
                 bytesPerRow:bytesPerRow
                  fromRegion:region
                 mipmapLevel:0];

    outPixels.allocate((int)width, (int)height, 4);
    unsigned char* dst = outPixels.getData();

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

bool internal::captureWindowToFile(const std::filesystem::path& path) {
    if (path.is_relative()) {
        return internal::captureWindowToFile(getDataPath(path));
    }
    Pixels pixels;
    if (!captureWindow(pixels)) {
        return false;
    }

    int width = pixels.getWidth();
    int height = pixels.getHeight();
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

    CGContextRef context = CGBitmapContextCreate(
        pixels.getData(),
        width, height,
        8,
        width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast | (CGBitmapInfo)kCGBitmapByteOrder32Big
    );

    if (!context) {
        CGColorSpaceRelease(colorSpace);
        logError() << "[Screenshot] Failed to create CGContext";
        return false;
    }

    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    if (!cgImage) {
        logError() << "[Screenshot] Failed to create CGImage";
        return false;
    }

    // Use UIImage + PNG/JPEG representation
    UIImage* image = [UIImage imageWithCGImage:cgImage];
    CGImageRelease(cgImage);

    if (!image) {
        logError() << "[Screenshot] Failed to create UIImage";
        return false;
    }

    NSData* data = nil;
    std::string ext = path.extension().string();
    if (ext == ".jpg" || ext == ".jpeg") {
        data = UIImageJPEGRepresentation(image, 0.9);
    } else {
        data = UIImagePNGRepresentation(image);
    }

    if (!data) {
        logError() << "[Screenshot] Failed to create image data";
        return false;
    }

    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    BOOL success = [data writeToFile:nsPath atomically:YES];

    if (success) {
        logVerbose() << "[Screenshot] Saved: " << path;
    } else {
        logError() << "[Screenshot] Failed to save: " << path;
    }

    return success;
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
    [session setActive:YES error:nil];
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
