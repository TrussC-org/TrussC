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

// ---------------------------------------------------------------------------
// Screenshot — not implemented on web (#230)
// ---------------------------------------------------------------------------
// Nothing in the browser build reads the canvas back, and #230 decided not
// to add it: screenshots on web are taken with the browser's own tools. Adding
// it would mean, per backend:
//   - WGPU (default): the swapchain cannot be read back synchronously (the
//     same limitation as Fbo::readPixelsPlatform in tcFbo_web.cpp), so it
//     would need new JS glue, e.g. a canvas.toBlob() download.
//   - GLES3 (TC_WEB_BACKEND=GLES3): glReadPixels right after present(), as on
//     Linux/Android, may be enough, but it is neither wired up nor tested.
// So every capture entry point fails honestly: it returns false and warns
// once per API, so an app that calls grabScreen() or saveScreenshot() every
// frame does not flood the browser console.
static bool captureWindowWarned_ = false;
static bool captureWindowToFileWarned_ = false;

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

// Reached from saveScreenshot(), which on web skips the deferred queue and
// comes straight here (see TrussC.h).
bool internal::captureWindowToFile(const std::filesystem::path& path) {
    (void)path;
    if (!captureWindowToFileWarned_) {
        captureWindowToFileWarned_ = true;
        logWarning("Screenshot") << "saveScreenshot() is not implemented on "
            "web (no canvas readback): no file is written (returns false). Use "
            "the browser's own screenshot feature instead.";
    }
    return false;
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
