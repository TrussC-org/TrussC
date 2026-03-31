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
namespace platform {

float getDisplayScaleFactor() {
    return (float)[UIScreen mainScreen].scale;
}

void setWindowSize(int width, int height) {
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
    sapp_swapchain sc = sapp_get_swapchain();
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)sc.metal.current_drawable;
    if (!drawable) {
        logError() << "[Screenshot] Failed to get Metal drawable";
        return false;
    }

    id<MTLTexture> texture = drawable.texture;
    if (!texture) {
        logError() << "[Screenshot] Failed to get Metal texture";
        return false;
    }

    NSUInteger width = texture.width;
    NSUInteger height = texture.height;

    outPixels.allocate((int)width, (int)height, 4);

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * 4;

    [texture getBytes:outPixels.getData()
          bytesPerRow:bytesPerRow
           fromRegion:region
          mipmapLevel:0];

    // BGRA -> RGBA conversion
    unsigned char* data = outPixels.getData();
    for (NSUInteger i = 0; i < width * height; i++) {
        unsigned char temp = data[i * 4 + 0];
        data[i * 4 + 0] = data[i * 4 + 2];
        data[i * 4 + 2] = temp;
    }

    return true;
}

bool saveScreenshot(const std::filesystem::path& path) {
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
    return (float)(heading * M_PI / 180.0);
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
} // namespace platform (temporarily close for extern)
} // namespace trussc
extern bool _sapp_ios_immersive_mode;
namespace trussc {
namespace platform {
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

} // namespace platform
} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
