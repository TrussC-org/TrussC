// =============================================================================
// macOS プラットフォーム固有機能
// =============================================================================

#include "TrussC.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioServices.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#import <CoreLocation/CoreLocation.h>

// sokol_app の swapchain 取得関数
#include "sokol_app.h"

namespace trussc {
namespace platform {

float getDisplayScaleFactor() {
    CGDirectDisplayID displayId = CGMainDisplayID();
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayId);

    if (!mode) {
        return 1.0f;
    }

    size_t pixelWidth = CGDisplayModeGetPixelWidth(mode);
    size_t pointWidth = CGDisplayModeGetWidth(mode);

    CGDisplayModeRelease(mode);

    if (pointWidth == 0) {
        return 1.0f;
    }

    return (float)pixelWidth / (float)pointWidth;
}

// Immersive mode (no-op on desktop)
void setImmersiveMode(bool enabled) { (void)enabled; }
bool getImmersiveMode() { return false; }

void setWindowSize(int width, int height) {
    // メインウィンドウを取得
    NSWindow* window = [[NSApplication sharedApplication] mainWindow];
    if (!window) {
        // mainWindow が nil の場合、最初のウィンドウを試す
        NSArray* windows = [[NSApplication sharedApplication] windows];
        if (windows.count > 0) {
            window = windows[0];
        }
    }

    if (window) {
        // 現在のフレームを取得
        NSRect frame = [window frame];

        // コンテンツ領域のサイズを変更（タイトルバーは維持）
        NSRect contentRect = [window contentRectForFrameRect:frame];
        contentRect.size.width = width;
        contentRect.size.height = height;

        // 新しいフレームを計算（左上を基準に維持）
        NSRect newFrame = [window frameRectForContentRect:contentRect];
        newFrame.origin.y = frame.origin.y + frame.size.height - newFrame.size.height;

        [window setFrame:newFrame display:YES animate:NO];
    }
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
// スクリーンショット機能（Metal API を使用）
// ---------------------------------------------------------------------------

bool captureWindow(Pixels& outPixels) {
    // sokol_app から現在の swapchain を取得
    sapp_swapchain sc = sapp_get_swapchain();
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)sc.metal.current_drawable;
    if (!drawable) {
        logError() << "[Screenshot] Metal drawable が取得できません";
        return false;
    }

    id<MTLTexture> texture = drawable.texture;
    if (!texture) {
        logError() << "[Screenshot] Metal テクスチャが取得できません";
        return false;
    }

    NSUInteger width = texture.width;
    NSUInteger height = texture.height;
    MTLPixelFormat pixelFormat = texture.pixelFormat;

    // Read raw pixel data from Metal texture
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * 4;
    std::vector<uint8_t> rawData(bytesPerRow * height);

    [texture getBytes:rawData.data()
          bytesPerRow:bytesPerRow
           fromRegion:region
          mipmapLevel:0];

    // Allocate output pixels (always RGBA8)
    outPixels.allocate((int)width, (int)height, 4);
    unsigned char* dst = outPixels.getData();

    if (pixelFormat == MTLPixelFormatRGB10A2Unorm) {
        // RGB10A2 bit layout: [A:2 (31-30)][B:10 (29-20)][G:10 (19-10)][R:10 (9-0)]
        const uint32_t* src = (const uint32_t*)rawData.data();
        for (NSUInteger i = 0; i < width * height; i++) {
            uint32_t pixel = src[i];
            uint32_t r10 = (pixel >>  0) & 0x3FF;  // bits 0-9
            uint32_t g10 = (pixel >> 10) & 0x3FF;  // bits 10-19
            uint32_t b10 = (pixel >> 20) & 0x3FF;  // bits 20-29
            uint32_t a2  = (pixel >> 30) & 0x3;    // bits 30-31
            // Convert 10-bit (0-1023) to 8-bit, 2-bit (0-3) to 8-bit
            dst[i * 4 + 0] = (uint8_t)(r10 >> 2);
            dst[i * 4 + 1] = (uint8_t)(g10 >> 2);
            dst[i * 4 + 2] = (uint8_t)(b10 >> 2);
            dst[i * 4 + 3] = (uint8_t)(a2 * 85);   // 0→0, 1→85, 2→170, 3→255
        }
    } else {
        // BGRA8 fallback
        memcpy(dst, rawData.data(), bytesPerRow * height);
        for (NSUInteger i = 0; i < width * height; i++) {
            unsigned char temp = dst[i * 4 + 0];  // B
            dst[i * 4 + 0] = dst[i * 4 + 2];     // R
            dst[i * 4 + 2] = temp;                // B
        }
    }

    return true;
}

bool saveScreenshot(const std::filesystem::path& path) {
    // まず Pixels にキャプチャ
    Pixels pixels;
    if (!captureWindow(pixels)) {
        return false;
    }

    // CGImage を作成
    int width = pixels.getWidth();
    int height = pixels.getHeight();
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

    CGContextRef context = CGBitmapContextCreate(
        pixels.getData(),
        width, height,
        8,                          // bitsPerComponent
        width * 4,                  // bytesPerRow
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );

    if (!context) {
        CGColorSpaceRelease(colorSpace);
        logError() << "[Screenshot] CGContext の作成に失敗しました";
        return false;
    }

    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    if (!cgImage) {
        logError() << "[Screenshot] CGImage の作成に失敗しました";
        return false;
    }

    // NSBitmapImageRep に変換
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
    CGImageRelease(cgImage);

    if (!rep) {
        logError() << "[Screenshot] NSBitmapImageRep の作成に失敗しました";
        return false;
    }

    // ファイル拡張子から形式を判定
    std::string ext = path.extension().string();
    NSBitmapImageFileType fileType = NSBitmapImageFileTypePNG;
    if (ext == ".jpg" || ext == ".jpeg") {
        fileType = NSBitmapImageFileTypeJPEG;
    } else if (ext == ".tiff" || ext == ".tif") {
        fileType = NSBitmapImageFileTypeTIFF;
    } else if (ext == ".bmp") {
        fileType = NSBitmapImageFileTypeBMP;
    } else if (ext == ".gif") {
        fileType = NSBitmapImageFileTypeGIF;
    }

    // ファイルに保存
    NSData* data = [rep representationUsingType:fileType properties:@{}];
    if (!data) {
        logError() << "[Screenshot] 画像データの作成に失敗しました";
        return false;
    }

    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    BOOL success = [data writeToFile:nsPath atomically:YES];

    if (success) {
        logVerbose() << "[Screenshot] 保存完了: " << path;
    } else {
        logError() << "[Screenshot] 保存に失敗しました: " << path;
    }

    return success;
}

// ---------------------------------------------------------------------------
// System sensors (stubs for macOS — most are mobile-only)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// System Volume (CoreAudio)
// ---------------------------------------------------------------------------
static AudioDeviceID _tcGetDefaultOutputDevice() {
    AudioDeviceID deviceId = 0;
    UInt32 size = sizeof(deviceId);
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &deviceId);
    return deviceId;
}

float getSystemVolume() {
    AudioDeviceID device = _tcGetDefaultOutputDevice();
    if (device == 0) return -1.0f;

    Float32 volume = 0;
    UInt32 size = sizeof(volume);
    AudioObjectPropertyAddress addr = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    OSStatus status = AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, &volume);
    if (status != noErr) return -1.0f;
    return (float)volume;
}

void setSystemVolume(float volume) {
    AudioDeviceID device = _tcGetDefaultOutputDevice();
    if (device == 0) return;

    Float32 vol = std::clamp(volume, 0.0f, 1.0f);
    AudioObjectPropertyAddress addr = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    AudioObjectSetPropertyData(device, &addr, 0, nullptr, sizeof(vol), &vol);
}

// ---------------------------------------------------------------------------
// Screen Brightness (CoreGraphics private API)
// ---------------------------------------------------------------------------
extern "C" {
    double CoreDisplay_Display_GetUserBrightness(CGDirectDisplayID id);
    void CoreDisplay_Display_SetUserBrightness(CGDirectDisplayID id, double brightness);
}

float getSystemBrightness() {
    double b = CoreDisplay_Display_GetUserBrightness(CGMainDisplayID());
    return (float)b;
}

void setSystemBrightness(float brightness) {
    CoreDisplay_Display_SetUserBrightness(CGMainDisplayID(), (double)std::clamp(brightness, 0.0f, 1.0f));
}

ThermalState getThermalState() {
    NSProcessInfoThermalState state = [NSProcessInfo processInfo].thermalState;
    switch (state) {
        case NSProcessInfoThermalStateFair:     return ThermalState::Fair;
        case NSProcessInfoThermalStateSerious:  return ThermalState::Serious;
        case NSProcessInfoThermalStateCritical: return ThermalState::Critical;
        default: return ThermalState::Nominal;
    }
}
float getThermalTemperature() { return -1.0f; }

// ---------------------------------------------------------------------------
// Battery (IOPowerSources)
// ---------------------------------------------------------------------------
float getBatteryLevel() {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return -1.0f;

    CFArrayRef sources = IOPSCopyPowerSourcesList(info);
    if (!sources || CFArrayGetCount(sources) == 0) {
        if (sources) CFRelease(sources);
        CFRelease(info);
        return -1.0f;
    }

    float level = -1.0f;
    CFDictionaryRef ps = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(sources, 0));
    if (ps) {
        CFNumberRef capacityRef = (CFNumberRef)CFDictionaryGetValue(ps, CFSTR(kIOPSCurrentCapacityKey));
        CFNumberRef maxRef = (CFNumberRef)CFDictionaryGetValue(ps, CFSTR(kIOPSMaxCapacityKey));
        if (capacityRef && maxRef) {
            int capacity = 0, maxCapacity = 0;
            CFNumberGetValue(capacityRef, kCFNumberIntType, &capacity);
            CFNumberGetValue(maxRef, kCFNumberIntType, &maxCapacity);
            if (maxCapacity > 0) level = (float)capacity / (float)maxCapacity;
        }
    }

    CFRelease(sources);
    CFRelease(info);
    return level;
}

bool isBatteryCharging() {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return false;

    CFArrayRef sources = IOPSCopyPowerSourcesList(info);
    if (!sources || CFArrayGetCount(sources) == 0) {
        if (sources) CFRelease(sources);
        CFRelease(info);
        return false;
    }

    bool charging = false;
    CFDictionaryRef ps = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(sources, 0));
    if (ps) {
        CFStringRef state = (CFStringRef)CFDictionaryGetValue(ps, CFSTR(kIOPSPowerSourceStateKey));
        if (state && CFStringCompare(state, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) {
            charging = true;
        }
    }

    CFRelease(sources);
    CFRelease(info);
    return charging;
}

Vec3 getAccelerometer() { return Vec3(0, 0, 0); }
Vec3 getGyroscope() { return Vec3(0, 0, 0); }
Quaternion getDeviceOrientation() { return Quaternion(1, 0, 0, 0); }
float getCompassHeading() { return 0.0f; }

bool isProximityClose() { return false; }

} // namespace platform
} // namespace trussc

// ---------------------------------------------------------------------------
// Location (CoreLocation) — ObjC declarations must be at global scope
// ---------------------------------------------------------------------------
static trussc::Location _macLastLocation;
static bool _macLocationStarted = false;

@interface _TCMacLocationDelegate : NSObject <CLLocationManagerDelegate>
@end
static _TCMacLocationDelegate* _macLocationDelegate = nil;
static CLLocationManager* _macLocationManager = nil;

@implementation _TCMacLocationDelegate
- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations {
    CLLocation* loc = locations.lastObject;
    if (loc) {
        _macLastLocation.latitude = loc.coordinate.latitude;
        _macLastLocation.longitude = loc.coordinate.longitude;
        _macLastLocation.altitude = loc.altitude;
        _macLastLocation.accuracy = (float)loc.horizontalAccuracy;
    }
}
- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error {
    trussc::logWarning() << "[Location] " << [[error localizedDescription] UTF8String];
}
- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
    if (manager.authorizationStatus == kCLAuthorizationStatusAuthorized) {
        [manager startUpdatingLocation];
    }
}
@end

namespace trussc {
namespace platform {

Location getLocation() {
    if (!_macLocationStarted) {
        _macLocationStarted = true;
        _macLocationDelegate = [[_TCMacLocationDelegate alloc] init];
        _macLocationManager = [[CLLocationManager alloc] init];
        _macLocationManager.delegate = _macLocationDelegate;
        _macLocationManager.desiredAccuracy = kCLLocationAccuracyBest;
        if ([_macLocationManager respondsToSelector:@selector(requestAlwaysAuthorization)]) {
            [_macLocationManager requestAlwaysAuthorization];
        }
        [_macLocationManager startUpdatingLocation];
    }
    return _macLastLocation;
}

} // namespace platform
} // namespace trussc

#endif // __APPLE__
