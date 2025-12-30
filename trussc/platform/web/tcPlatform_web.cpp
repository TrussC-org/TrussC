// =============================================================================
// Emscripten/Web プラットフォーム固有機能
// =============================================================================

#ifdef __EMSCRIPTEN__

#include "TrussC.h"
#include <emscripten.h>
#include <emscripten/html5.h>

namespace trussc {
namespace platform {

float getDisplayScaleFactor() {
    // ブラウザの devicePixelRatio を取得
    return (float)emscripten_get_device_pixel_ratio();
}

void setWindowSize(int width, int height) {
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

} // namespace platform
} // namespace trussc

#endif // __EMSCRIPTEN__
