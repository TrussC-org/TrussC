// =============================================================================
// Emscripten/Web プラットフォーム固有機能
// =============================================================================

#ifdef __EMSCRIPTEN__

#include "TrussC.h"
#include <emscripten.h>
#include <emscripten/html5.h>

namespace trussc {

float getDisplayScaleFactor() {
    // ブラウザの devicePixelRatio を取得
    return (float)emscripten_get_device_pixel_ratio();
}

// Immersive mode (no-op on web)
void setImmersiveMode(bool enabled) { (void)enabled; }
bool getImmersiveMode() { return false; }

// Orientation (no-op on web)
void setOrientation(Orientation mask) { (void)mask; }

// Keep screen on (TODO: Screen Wake Lock API via JS — no-op for now)
void setKeepScreenOn(bool enabled) { (void)enabled; }
bool getKeepScreenOn() { return false; }

IVec2 getWindowPosition() {
    logWarning("Platform") << "getWindowPosition() is not supported on Web";
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() is not supported on Web";
    (void)x; (void)y;
}

void setWindowDecorated(bool decorated) {
    // No window decorations to toggle on Web.
    (void)decorated;
}

void setWindowSizeLogical(int width, int height) {
    // Emscripten では canvas サイズを変更
    // sokol_app が使用する canvas ID を指定
    emscripten_set_canvas_element_size("canvas", width, height);
}

fs::path getExecutablePath() {
    return fs::path("/");
}

fs::path getExecutableDir() {
    return fs::path("/");
}

// User data / temp folders: the browser has no persistent file system here,
// so both live in Emscripten's in-memory file system and are gone on reload.
fs::path internal::platformUserDataRoot() {
    return fs::path("/userdata");
}

fs::path internal::platformTempRoot() {
    return fs::path("/tmp");
}

fs::path internal::platformAppBundlePath() {
    return {};
}

// ---------------------------------------------------------------------------
// Screenshot — deferred canvas download; synchronous Pixels stays unsupported.
// ---------------------------------------------------------------------------
static bool captureWindowWarned_ = false;

// Invoked by the afterFrame drain, before returning to the browser. toBlob()
// snapshots the rendered canvas now, even though its callback runs later, so
// neither WebGPU nor WebGL2 needs preserveDrawingBuffer.
using ScreenshotErrorCallback = void (*)(int);
EM_JS_DEPS(screenshotDownloadDeps, "$UTF8ToString,$getWasmTableEntry");
EM_JS(bool, downloadScreenshot, (const char* namePtr, const char* mimePtr,
                                 ScreenshotErrorCallback onError), {
    const name = UTF8ToString(namePtr);
    const mime = UTF8ToString(mimePtr);
    const fail = (reason) => getWasmTableEntry(onError)(reason);
    try {
        const canvas = Module['canvas'] || document.querySelector('#canvas');
        if (!canvas) { fail(0); return false; }
        canvas.toBlob((blob) => {
            if (!blob) { fail(2); return; }
            let url;
            let link;
            try {
                url = URL.createObjectURL(blob);
                link = document.createElement('a');
                link.href = url;
                link.download = name;
                document.body.appendChild(link);
                link.click();
            } catch (error) {
                fail(3);
            } finally {
                if (link) link.remove();
                // Keep the URL alive until the browser has consumed the click.
                if (url) setTimeout(() => URL.revokeObjectURL(url), 1000);
            }
        }, mime);
        return true;
    } catch (error) {
        fail(1);
        return false;
    }
});

static void screenshotDownloadError(int reason) {
    const char* message = reason == 0 ? "Screenshot canvas is unavailable" :
                          reason == 1 ? "Canvas screenshot failed (the canvas may be tainted by cross-origin content)" :
                          reason == 2 ? "Canvas screenshot encoding returned no image" :
                                        "Could not start the screenshot download";
    logError("Screenshot") << message;
}

bool captureWindow(Pixels& outPixels) {
    (void)outPixels;
    if (!captureWindowWarned_) {
        captureWindowWarned_ = true;
        logWarning("Screenshot") << "grabScreen()/captureWindow() is not "
            "implemented on web (no canvas readback): no pixels are returned "
            "(returns false). Use the browser's own screenshot feature instead.";
    }
    return false;
}

bool internal::saveScreenshotPixels(const Pixels& pixels, const std::filesystem::path& path) {
    return pixels.save(internal::resolveScreenshotPath(path));
}

std::filesystem::path internal::resolveScreenshotDownloadName(const std::filesystem::path& path) {
    auto name = path.filename();
    if (name.empty()) name = "screenshot-" + getTimestampString() + ".png";
    const auto ext = toLower(getFileExtension(name));
    if (ext != "png" && ext != "jpg" && ext != "jpeg") {
        name += ".png";
        logWarning("Screenshot") << "Unsupported or missing extension; saving PNG to "
                                 << internal::pathToUtf8(name) << ". Supported formats: png, jpg/jpeg";
    }
    return name;
}

bool internal::captureWindowToFile(const std::filesystem::path& path) {
    const auto resolved = internal::resolveScreenshotDownloadName(path);
    const auto name = internal::pathToUtf8(resolved);
    const auto ext = toLower(getFileExtension(resolved));
    return downloadScreenshot(name.c_str(),
                              (ext == "jpg" || ext == "jpeg") ? "image/jpeg" : "image/png",
                              screenshotDownloadError);
}

// ---------------------------------------------------------------------------
// App menu (macOS only) — stub
// ---------------------------------------------------------------------------
namespace internal {
void installAppMenu() {}
} // namespace internal

// ---------------------------------------------------------------------------
// System sensors (stubs)
// ---------------------------------------------------------------------------
float getSystemVolume() { return -1.0f; }
void setSystemVolume(float volume) { (void)volume; }
float getSystemBrightness() { return -1.0f; }
void setSystemBrightness(float brightness) { (void)brightness; }
ThermalState getThermalState() { return ThermalState::Nominal; }
float getThermalTemperature() { return -1.0f; }
float getBatteryLevel() { return -1.0f; }
bool isBatteryCharging() { return false; }
Vec3 getAccelerometer() { return Vec3(0, 0, 0); }
Vec3 getGyroscope() { return Vec3(0, 0, 0); }
Quaternion getDeviceOrientation() { return Quaternion(1, 0, 0, 0); }
float getCompassHeading() { return 0.0f; }
bool isProximityClose() { return false; }
Location getLocation() { return Location(); }

void bringWindowToFront() {
    // no-op: web apps have no window management
}

} // namespace trussc

#endif // __EMSCRIPTEN__
