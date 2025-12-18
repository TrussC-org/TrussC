#pragma once

#include <string>
#include <filesystem>

// =============================================================================
// プラットフォーム固有機能
// =============================================================================

namespace trussc {

// 前方宣言
class Pixels;

namespace platform {

// メインディスプレイのDPIスケールを取得（ウィンドウ作成前でも使用可能）
// macOS: 1.0 (通常) or 2.0 (Retina)
// その他: 1.0
float getDisplayScaleFactor();

// ウィンドウサイズを変更（論理サイズで指定）
// macOS: NSWindow を使用
void setWindowSize(int width, int height);

// 実行ファイルの絶対パスを取得
std::string getExecutablePath();

// 実行ファイルがあるディレクトリを取得（末尾に / 付き）
std::string getExecutableDir();

// ---------------------------------------------------------------------------
// スクリーンショット機能
// ---------------------------------------------------------------------------

// 現在のウィンドウをキャプチャして Pixels に格納
// 成功時は true、失敗時は false を返す
bool captureWindow(Pixels& outPixels);

// 現在のウィンドウをキャプチャしてファイルに保存
// 成功時は true、失敗時は false を返す
bool saveScreenshot(const std::filesystem::path& path);

} // namespace platform
} // namespace trussc
