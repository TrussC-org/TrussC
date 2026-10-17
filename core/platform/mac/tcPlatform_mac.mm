// =============================================================================
// macOS プラットフォーム固有機能
// =============================================================================

#include "TrussC.h"
#include <mutex>
#include <vector>

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioServices.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#import <CoreLocation/CoreLocation.h>

// sokol_app の swapchain 取得関数
#include "sokol_app_tc.h"

namespace trussc {

void bringWindowToFront() {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    if (window) {
        [NSApp activateIgnoringOtherApps:YES];
        [window makeKeyAndOrderFront:nil];
    }
}

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

// Orientation (no-op on desktop)
void setOrientation(Orientation mask) { (void)mask; }

// Keep screen on (IOPMAssertion)
static bool keepScreenOn_ = false;
static IOPMAssertionID keepScreenOnAssertion_ = kIOPMNullAssertionID;

void setKeepScreenOn(bool enabled) {
    if (enabled == keepScreenOn_) return;
    if (enabled) {
        IOReturn r = IOPMAssertionCreateWithName(
            kIOPMAssertionTypeNoDisplaySleep,
            kIOPMAssertionLevelOn,
            CFSTR("TrussC keepScreenOn"),
            &keepScreenOnAssertion_);
        if (r == kIOReturnSuccess) keepScreenOn_ = true;
    } else {
        if (keepScreenOnAssertion_ != kIOPMNullAssertionID) {
            IOPMAssertionRelease(keepScreenOnAssertion_);
            keepScreenOnAssertion_ = kIOPMNullAssertionID;
        }
        keepScreenOn_ = false;
    }
}

bool getKeepScreenOn() { return keepScreenOn_; }

IVec2 getWindowPosition() {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    if (!window) return IVec2(-1, -1);

    NSRect frame = [window frame];
    // macOS coordinate system is bottom-left origin; convert to top-left
    NSScreen* screen = [window screen] ? [window screen] : [NSScreen mainScreen];
    float screenHeight = (float)screen.frame.size.height;
    int x = (int)frame.origin.x;
    int y = (int)(screenHeight - frame.origin.y - frame.size.height);
    return IVec2(x, y);
}

void setWindowPosition(int x, int y) {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    if (!window) return;

    // Convert top-left origin to macOS bottom-left origin
    NSScreen* screen = [window screen] ? [window screen] : [NSScreen mainScreen];
    float screenHeight = (float)screen.frame.size.height;
    NSRect frame = [window frame];
    NSPoint origin;
    origin.x = x;
    origin.y = screenHeight - y - frame.size.height;
    [window setFrameOrigin:origin];
}

void setWindowDecorated(bool decorated) {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    if (!window) return;

    const NSWindowStyleMask base = NSWindowStyleMaskTitled |
                                   NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskMiniaturizable |
                                   NSWindowStyleMaskResizable;

    if (decorated) {
        window.styleMask = base;
        window.titlebarAppearsTransparent = NO;
        window.titleVisibility = NSWindowTitleVisible;
        window.movableByWindowBackground = NO;
        window.hasShadow = YES;
        [[window standardWindowButton:NSWindowCloseButton]       setHidden:NO];
        [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
        [[window standardWindowButton:NSWindowZoomButton]        setHidden:NO];
    } else {
        // Keep the window *titled* (so it can become key AND stays closable —
        // sokol's quit path calls performClose:, which no-ops on a borderless
        // window) but hide all chrome with a transparent, full-size-content
        // title bar. The result looks borderless yet keyboard + exitApp() work.
        window.styleMask = base | NSWindowStyleMaskFullSizeContentView;
        window.titlebarAppearsTransparent = YES;
        window.titleVisibility = NSWindowTitleHidden;
        window.movableByWindowBackground = YES;
        [[window standardWindowButton:NSWindowCloseButton]       setHidden:YES];
        [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
        [[window standardWindowButton:NSWindowZoomButton]        setHidden:YES];

        // macOS (Tahoe+) auto-rounds window corners; the Metal layer is square,
        // so paint the backdrop black and drop the shadow to avoid a glowing
        // corner/edge highlight.
        window.backgroundColor = [NSColor blackColor];
        window.opaque = YES;
        window.hasShadow = NO;
    }

    // Re-assert key + first responder so mouse and keyboard keep flowing.
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:window.contentView];
}

void setWindowSizeLogical(int width, int height) {
    // The TrussC main window, like the other functions in this file. NOT
    // [NSApp mainWindow]: that is AppKit's focused window, which is a
    // secondary window whenever one has focus.
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();

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

fs::path getExecutablePath() {
    NSString* path = [[NSBundle mainBundle] executablePath];
    return fs::path([path UTF8String]);
}

fs::path getExecutableDir() {
    return getExecutablePath().parent_path();
}

// ---------------------------------------------------------------------------
// User data / temp folders (getUserDataPath / getTempPath)
// ---------------------------------------------------------------------------
namespace {
// The bundle id names the app's folder (the executable name for a binary
// that does not run from a bundle).
fs::path appFolderName() {
    NSString* bundleId = [[NSBundle mainBundle] bundleIdentifier];
    if (bundleId.length > 0) return fs::path(bundleId.UTF8String);
    fs::path name = getExecutablePath().filename();
    return name.empty() ? fs::path("TrussC") : name;
}
} // namespace

fs::path internal::platformUserDataRoot() {
    NSString* support = [NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES) firstObject];
    fs::path base = support ? fs::path(support.UTF8String)
                            : fs::path(NSHomeDirectory().UTF8String) / "Library" / "Application Support";
    return base / appFolderName();
}

fs::path internal::platformTempRoot() {
    // NSTemporaryDirectory() is $TMPDIR (the per-user temp folder)
    return fs::path(NSTemporaryDirectory().UTF8String) / appFolderName();
}

fs::path internal::platformAppBundlePath() {
    NSString* bundle = [[NSBundle mainBundle] bundlePath];
    if (!bundle || ![bundle.pathExtension isEqualToString:@"app"]) return {};
    return fs::path(bundle.UTF8String);
}

// ---------------------------------------------------------------------------
// スクリーンショット機能（Metal API を使用）
// ---------------------------------------------------------------------------

// Copy a completed staging readback into the caller's destination buffer with a
// single getBytes (no intermediate buffer for the common BGRA8 case). For BGRA8
// + wantRGBA=false (the recorder feeding a BGRA CVPixelBuffer) this is one copy,
// zero swizzle. Runs on whatever thread the consumer is on.
internal::RecorderFrameLayout internal::CaptureReadback::getLayout() const {
    id<MTLTexture> tex = (__bridge id<MTLTexture>)staging;
    // Bound (abi): Metal staging extent must match the captured frame. Overflow drops the recorder frame and is counted.
    if (!tex || width <= 0 || height <= 0 || tex.width != (NSUInteger)width ||
        tex.height != (NSUInteger)height) return {};
    const auto format = tex.pixelFormat;
    const bool packed10 = format == MTLPixelFormatRGB10A2Unorm;
    // Bound (abi): Metal staging format must match the four-byte capture layout. Overflow drops the recorder frame and is counted.
    if (packed10 != isRGB10A2 || (!packed10 && format != MTLPixelFormatBGRA8Unorm &&
                                 format != MTLPixelFormatBGRA8Unorm_sRGB)) return {};
    return internal::recorderPackedLayout(tex.width, tex.height,
        packed10 ? internal::RecorderPixelFormat::RGB10A2 : internal::RecorderPixelFormat::BGRA8);
}

bool internal::CaptureReadback::readInto(unsigned char* dst, size_t dstStride,
                                         size_t capacity, bool wantRGBA) const {
    const auto source = getLayout();
    auto destination = source;
    destination.rowBytes = dstStride;
    destination.byteSize = capacity;
    // Bound (abi): the native readback destination must hold every captured row. Overflow drops the recorder frame and is counted.
    if (!dst || !internal::recorderFrameFits(destination, source.width, source.height, source.format))
        return false;
    id<MTLTexture> tex = (__bridge id<MTLTexture>)staging;
    const NSUInteger w = (NSUInteger)width;
    const NSUInteger h = (NSUInteger)height;

    if (isRGB10A2) {
        // 10-bit packed: must unpack via a temp. Layout per 32-bit pixel:
        // [A:2 (31-30)][B:10 (29-20)][G:10 (19-10)][R:10 (9-0)].
        std::vector<uint8_t> raw((size_t)w * 4 * h);
        [tex getBytes:raw.data()
          bytesPerRow:w * 4
           fromRegion:MTLRegionMake2D(0, 0, w, h)
          mipmapLevel:0];
        const uint32_t* src = (const uint32_t*)raw.data();
        for (NSUInteger y = 0; y < h; y++) {
            uint8_t* drow = dst + (size_t)y * dstStride;
            for (NSUInteger x = 0; x < w; x++) {
                uint32_t p = src[y * w + x];
                uint8_t r = (uint8_t)(((p >>  0) & 0x3FF) >> 2);
                uint8_t g = (uint8_t)(((p >> 10) & 0x3FF) >> 2);
                uint8_t b = (uint8_t)(((p >> 20) & 0x3FF) >> 2);
                uint8_t a = (uint8_t)(((p >> 30) & 0x3) * 85);
                if (wantRGBA) { drow[x*4+0]=r; drow[x*4+1]=g; drow[x*4+2]=b; drow[x*4+3]=a; }
                else          { drow[x*4+0]=b; drow[x*4+1]=g; drow[x*4+2]=r; drow[x*4+3]=a; }
            }
        }
        return true;
    }

    // BGRA8: getBytes straight into dst (respecting the destination row stride).
    [tex getBytes:dst
      bytesPerRow:dstStride
       fromRegion:MTLRegionMake2D(0, 0, w, h)
      mipmapLevel:0];
    if (wantRGBA) {
        // In-place B<->R swap (no extra buffer).
        for (NSUInteger y = 0; y < h; y++) {
            uint8_t* drow = dst + (size_t)y * dstStride;
            for (NSUInteger x = 0; x < w; x++) {
                uint8_t b = drow[x*4+0];
                drow[x*4+0] = drow[x*4+2];
                drow[x*4+2] = b;
            }
        }
    }
    return true;
}

// Free staging textures, returned by each completion after its readback.
struct internal::CaptureStagingPool {
    std::mutex mutex;
    std::vector<id<MTLTexture>> free;
};

// Host-resident operation; native completion owns only these resources and
// callbacks. Empty it before publishing the recorder's terminal count.
namespace {
struct NativeCaptureJob {
    std::shared_ptr<mcp::internal::WindowCaptureAdmission::Ownership> admission;
    id<MTLTexture> source = nil, staging = nil;
    std::shared_ptr<internal::CaptureStagingPool> pool;
    std::function<void(const internal::CaptureReadback&)> completion;
    std::function<void()> finished;
};
} // namespace

std::shared_ptr<internal::CaptureStagingPool> internal::makeCaptureStagingPool() {
    return std::make_shared<internal::CaptureStagingPool>();
}

std::pair<size_t, size_t> internal::captureWindowSize(const internal::WindowContext& ctx) {
    // Admission also runs in the next frame's prelude, before any drawable
    // exists. Query native framebuffer dimensions without acquiring or messaging
    // the borrowed drawable from a swapchain pass.
    if (!ctx.isMain) return {size_t(std::max(0, ctx.fbWidth)), size_t(std::max(0, ctx.fbHeight))};
    return {size_t(std::max(0, sapp_width())), size_t(std::max(0, sapp_height()))};
}

bool internal::captureWindowAsync(
    const std::function<void(const internal::CaptureReadback&)>& completion,
    const std::shared_ptr<internal::CaptureStagingPool>& pool,
    const std::function<void()>& finished) {
    // Select only this native tick's rendered drawable. The host clears the
    // borrowed pointer at frame end; this job owns its texture until cleanup.
    if (!sg_isvalid() || internal::hasPublicFboActivation()) return false;
    const void* drawablePtr = internal::windowCaptureSource().drawable;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)drawablePtr;
    if (!drawable) return false;
    id<MTLTexture> srcTexture = drawable.texture;
    if (!srcTexture) return false;

    NSUInteger width  = srcTexture.width;
    NSUInteger height = srcTexture.height;
    mcp::internal::reconcileWindowCapture(width, height);
    MTLPixelFormat pixelFormat = srcTexture.pixelFormat;
    // Bound (abi): Metal extents must fit the capture int dimensions. Overflow drops the recorder frame and is counted.
    if (width > (NSUInteger)std::numeric_limits<int>::max() ||
        height > (NSUInteger)std::numeric_limits<int>::max()) return false;
    const auto layout = internal::recorderPackedLayout(width, height,
        pixelFormat == MTLPixelFormatRGB10A2Unorm ? internal::RecorderPixelFormat::RGB10A2 : internal::RecorderPixelFormat::BGRA8);
    // Bound (abi): selected Metal source bytes and format must fit capture staging. Overflow drops the recorder frame and is counted.
    if (!layout.byteSize || (pixelFormat != MTLPixelFormatBGRA8Unorm &&
        pixelFormat != MTLPixelFormatBGRA8Unorm_sRGB && pixelFormat != MTLPixelFormatRGB10A2Unorm)) return false;
    auto admission = mcp::internal::WindowCaptureAdmission::retain();
    id<MTLDevice> device = srcTexture.device;
    if ((__bridge const void*)device != sg_mtl_device()) return false;

    // sokol sets framebufferOnly=YES on the layer, so getBytes on the drawable
    // asserts: blit into a shared-storage staging texture first. Each capture
    // owns its staging texture until its completion has finished reading; only
    // then does it go back to the pool. A fixed ring could wrap while an older
    // readback still used a slot (#361).
    id<MTLTexture> staging = nil;
    if (pool) {
        std::lock_guard<std::mutex> lock(pool->mutex);
        while (!staging && !pool->free.empty()) {
            id<MTLTexture> t = pool->free.back();
            pool->free.pop_back();
            // A texture of another size or device (window resized or moved)
            // is dropped here.
            if (t.device == device && t.width == width && t.height == height
                && t.pixelFormat == pixelFormat) {
                staging = t;
            }
        }
    }
    if (!staging) {
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModeShared;
        staging = [device newTextureWithDescriptor:desc];
    }
    if (!staging) return false;

    std::shared_ptr<NativeCaptureJob> job;
    try {
        job = std::make_shared<NativeCaptureJob>();
        job->admission = std::move(admission);
        job->source = srcTexture; job->staging = staging; job->pool = pool;
        job->completion = completion; job->finished = finished;
    } catch (const std::bad_alloc&) { return false; }

    // Stage in the renderer's prefix before any later source write. This also
    // handles a parked foreign encoder; submission never presents a drawable.
    internal::parkWindowPass();
    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)sg_tc_mtl_command_buffer();
    if (!cmdBuf || !sg_mtl_command_queue()) return false;
    id<MTLBlitCommandEncoder> blitEnc = [cmdBuf blitCommandEncoder];
    if (!blitEnc) return false;
    [blitEnc copyFromTexture:srcTexture
                 sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:staging
            destinationSlice:0 destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blitEnc endEncoding];

    // Staging and source ownership survive unretained-reference command
    // buffers. Cleanup precedes terminal notification, so the next recorder
    // opportunity cannot allocate a second slot before this one returns.
    const bool isRGB10A2 = (pixelFormat == MTLPixelFormatRGB10A2Unorm);
    [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        internal::CaptureReadback rb;
        rb.staging = cb.status == MTLCommandBufferStatusCompleted
            ? (__bridge void*)job->staging : nullptr;
        rb.width = (int)width; rb.height = (int)height; rb.isRGB10A2 = isRGB10A2;
        mcp::internal::finishCaptureJob(*job, rb, [&] {
            if (job->pool) {
                std::lock_guard<std::mutex> lock(job->pool->mutex);
                try { job->pool->free.push_back(job->staging); }
                catch (...) { /* reuse is optional */ }
            }
            job->staging = nil; job->source = nil; job->pool.reset();
        });
    }];
    sg_tc_submit_prefix();
    return true;
}

bool captureWindow(Pixels& outPixels) {
    if (!sg_isvalid() || internal::hasPublicFboActivation()) {
        logError("Screenshot") << "Metal window capture requires an available window target";
        return false;
    }
    internal::CapturePrefixScope capturePhase;
    // Synchronous wrapper over the async primitive: issue the capture and block
    // until the pixels land. Screenshots / MCP / exit use this — they need the
    // bytes before returning (so the file is written / the response is ready).
    // Completion may outlive a bounded synchronous wait. Own its result rather
    // than capturing the caller's Pixels or stack by reference.
    struct Result : mcp::internal::CaptureCompletionGate { Pixels pixels; bool ok = false; };
    auto result = std::make_shared<Result>();
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    bool started = internal::captureWindowAsync(
        [result](const internal::CaptureReadback& rb) {
            result->complete([&] {
              try {
                if (rb.getLayout().byteSize) {
                    result->pixels.allocate(rb.width, rb.height, 4);
                    result->ok = rb.readInto(result->pixels.getData(), (size_t)rb.width * 4,
                        result->pixels.getTotalBytes(), /*wantRGBA=*/true);
                }
              } catch (...) { result->ok = false; }
            });
        }, {}, [sem] {
            // Publish completion only after source/staging and carried admission
            // have finished cleanup, including after a caller timeout.
            dispatch_semaphore_signal(sem);
        });
    if (!started) {
        capturePhase.leaveParked();
        logError() << "[Screenshot] Metal drawable が取得できません";
        return false;
    }
    // Bound (platform): a stalled Metal completion must not hold the macOS
    // frame thread indefinitely; ten seconds permits slow GPU work/recovery.
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0) {
        result->abandon();
        capturePhase.leaveParked();
        logError("Screenshot") << "Metal window capture completion timed out";
        return false;
    }
    if (!result->ok) {
        capturePhase.leaveParked();
        logError("Screenshot") << "Metal window capture readback failed";
        return false;
    }
    outPixels = std::move(result->pixels);
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

        // CGImage を作成
        int width = pixels.getWidth();
        int height = pixels.getHeight();
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

        CGContextRef context = CGBitmapContextCreate(
            const_cast<unsigned char*>(pixels.getData()),
            width, height,
            8,                          // bitsPerComponent
            width * 4,                  // bytesPerRow
            colorSpace,
            (CGBitmapInfo)kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
        );

        if (!context) {
            CGColorSpaceRelease(colorSpace);
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "CGContext の作成に失敗しました"), "Screenshot");
        }

        CGImageRef cgImage = CGBitmapContextCreateImage(context);
        CGContextRelease(context);
        CGColorSpaceRelease(colorSpace);

        if (!cgImage) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "CGImage の作成に失敗しました"), "Screenshot");
        }

        // NSBitmapImageRep に変換
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
        CGImageRelease(cgImage);

        if (!rep) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "NSBitmapImageRep の作成に失敗しました"), "Screenshot");
        }

        // ファイル拡張子から形式を判定
        // Case-insensitive extension match on the resolved destination
        std::string ext = toLower(getFileExtension(path));
        NSBitmapImageFileType fileType = NSBitmapImageFileTypePNG;
        if (ext == "jpg" || ext == "jpeg") {
            fileType = NSBitmapImageFileTypeJPEG;
        } else if (ext == "tiff" || ext == "tif") {
            fileType = NSBitmapImageFileTypeTIFF;
        } else if (ext == "bmp") {
            fileType = NSBitmapImageFileTypeBMP;
        } else if (ext == "gif") {
            fileType = NSBitmapImageFileTypeGIF;
        }

        // ファイルに保存
        NSData* data = [rep representationUsingType:fileType properties:@{}];
        if (!data) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed, "画像データの作成に失敗しました"), "Screenshot");
        }

        return internal::atomicWrite(path, data.bytes, data.length, "Screenshot", true);
    }
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

} // namespace trussc

// ---------------------------------------------------------------------------
// Application menu (NSMenu) — ObjC declarations must be at global scope
// ---------------------------------------------------------------------------

@interface _TCMacAppMenuTarget : NSObject
- (void)tcQuit:(id)sender;
- (void)tcShowAbout:(id)sender;
@end
static _TCMacAppMenuTarget* _macAppMenuTarget = nil;

@implementation _TCMacAppMenuTarget
- (void)tcQuit:(id)sender {
    (void)sender;
    // Route through sokol's cancellable quit path so user code can intercept
    // via events().exitRequested.
    sapp_request_quit();
}
- (void)tcShowAbout:(id)sender {
    (void)sender;
    // App name, icon, version and copyright are pulled from the bundle's
    // Info.plist automatically.
    [NSApp orderFrontStandardAboutPanel:@{}];
}
@end

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

// ---------------------------------------------------------------------------
// Application menu (NSMenu)
// ---------------------------------------------------------------------------

namespace internal {

void installAppMenu() {
    // Note: cannot short-circuit on existing mainMenu — macOS auto-creates
    // an empty placeholder menu when activationPolicy becomes Regular, so
    // the menu is always already non-nil by the time this runs. Just overwrite.
    if (_macAppMenuTarget == nil) {
        _macAppMenuTarget = [[_TCMacAppMenuTarget alloc] init];
    }

    NSString* appName = [[NSRunningApplication currentApplication] localizedName];
    if (appName.length == 0) {
        appName = [[NSProcessInfo processInfo] processName];
    }

    NSMenu* mainMenu = [[NSMenu alloc] init];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appMenuItem];

    NSMenu* appMenu = [[NSMenu alloc] init];

    // About <App>
    NSMenuItem* aboutItem = [[NSMenuItem alloc]
        initWithTitle:[NSString stringWithFormat:@"About %@", appName]
        action:@selector(tcShowAbout:)
        keyEquivalent:@""];
    [aboutItem setTarget:_macAppMenuTarget];
    [appMenu addItem:aboutItem];

    [appMenu addItem:[NSMenuItem separatorItem]];

    // Hide <App>  (Cmd+H)
    [appMenu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
                       action:@selector(hide:)
                keyEquivalent:@"h"];

    // Hide Others (Cmd+Option+H)
    NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"Hide Others"
                                                action:@selector(hideOtherApplications:)
                                         keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:(NSEventModifierFlagOption | NSEventModifierFlagCommand)];

    // Show All
    [appMenu addItemWithTitle:@"Show All"
                       action:@selector(unhideAllApplications:)
                keyEquivalent:@""];

    [appMenu addItem:[NSMenuItem separatorItem]];

    // Quit <App>  (Cmd+Q) — cancellable via events().exitRequested
    NSMenuItem* quitItem = [[NSMenuItem alloc]
        initWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
        action:@selector(tcQuit:)
        keyEquivalent:@"q"];
    [quitItem setTarget:_macAppMenuTarget];
    [appMenu addItem:quitItem];

    [appMenuItem setSubmenu:appMenu];
    [NSApp setMainMenu:mainMenu];
}

} // namespace internal

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

} // namespace trussc

#endif // __APPLE__
