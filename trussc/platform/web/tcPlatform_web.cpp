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

IVec2 getWindowPosition() {
    logWarning("Platform") << "getWindowPosition() is not supported on Web";
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() is not supported on Web";
    (void)x; (void)y;
}

void setWindowSizeLogical(int width, int height) {
    // Emscripten では canvas サイズを変更
    // sokol_app が使用する canvas ID を指定
    emscripten_set_canvas_element_size("canvas", width, height);
}

std::string getExecutablePath() {
    return "/";
}

std::string getExecutableDir() {
    return "/";
}

bool captureWindow(Pixels& outPixels) {
    // TODO: WebGL からピクセル読み取り
    logWarning() << "[Screenshot] Emscripten では未実装";
    return false;
}

bool saveScreenshot(const std::filesystem::path& path) {
    // TODO: ダウンロードとして保存
    logWarning() << "[Screenshot] Emscripten では未実装";
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
