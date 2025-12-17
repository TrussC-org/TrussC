# TrussC 設計ドキュメント

## Loop Architecture (Decoupled Update/Draw)

「Enumによるモード指定」を廃止し、Draw（描画）とUpdate（論理）それぞれのタイミングをメソッドベースで独立設定できるアーキテクチャを採用する。

### Draw Loop (Render Timing)

| メソッド | 動作 |
|---------|------|
| `tc::setDrawVsync(true)` | モニタのリフレッシュレートに同期（デフォルト） |
| `tc::setDrawFps(n)` (n > 0) | 固定FPSで動作。VSyncは自動的にOFFになる |
| `tc::setDrawFps(0)` (or negative) | 自動ループ停止。`tc::redraw()` 呼び出し時のみ描画 |

### Update Loop (Logic Timing)

| メソッド | 動作 |
|---------|------|
| `tc::syncUpdateToDraw(true)` | `update()` は `draw()` の直前に1回だけ呼ばれる（Coupled、デフォルト） |
| `tc::setUpdateFps(n)` (n > 0) | `update()` は `draw()` とは独立して、指定されたHzで定期的に呼ばれる（Decoupled） |
| `tc::setUpdateFps(0)` (or negative) | Updateループ停止。入力イベントや `tc::redraw()` のみで駆動する完全なイベント駆動型アプリ |

### Idle State (No Freeze)

- DrawとUpdateの両方が停止（FPS <= 0）していても、OSのイベントループは動作し続ける
- アプリはフリーズせず「待機状態（Idle）」となる
- `onMousePress` などのイベントハンドラは正常に発火する

### Standard Helpers

| ヘルパー | 動作 |
|---------|------|
| `tc::setFps(60)` | `setDrawFps(60)` + `syncUpdateToDraw(true)` を実行 |
| `tc::setVsync(true)` | `setDrawVsync(true)` + `syncUpdateToDraw(true)` を実行 |

### 使用例

```cpp
// 通常のゲームループ（60fps固定）
void tcApp::setup() {
    tc::setFps(60);
}

// VSync同期（デフォルト動作）
void tcApp::setup() {
    tc::setVsync(true);
}

// 省電力モード（イベント駆動）
void tcApp::setup() {
    tc::setDrawFps(0);  // 描画停止
}
void tcApp::onMousePress(...) {
    // 何か処理
    tc::redraw();  // 明示的に再描画
}

// 物理シミュレーション（固定タイムステップ）
void tcApp::setup() {
    tc::setDrawVsync(true);    // 描画はVSync
    tc::setUpdateFps(120);     // 物理更新は120Hz
}
```

### 現在の実装（廃止予定）

```cpp
// 旧API（廃止予定）
enum class LoopMode {
    Game,   // 毎フレーム自動的にupdate/drawが呼ばれる
    Demand  // redraw()が呼ばれた時だけupdate/drawが呼ばれる
};
tc::setLoopMode(LoopMode::Game);
tc::setLoopMode(LoopMode::Demand);
```

---

## 3D Projection

### Metal クリップ空間

macOS (Metal) では、クリップ空間の Z 範囲が OpenGL と異なる：
- OpenGL: Z = [-1, 1]
- Metal: Z = [0, 1]

`sgl_ortho` は OpenGL スタイルの行列を生成するため、Metal で深度テストが正しく動作しない。

### 解決策

3D 描画には `sgl_perspective` を使用する：

```cpp
tc::enable3DPerspective(fovY, nearZ, farZ);  // パースペクティブ + 深度テスト
// ... 3D描画 ...
tc::disable3D();  // 2D描画に戻す
```

2D 描画は従来通り `sgl_ortho` を使用（深度テスト不要）。

---

## Module / Addon アーキテクチャ

TrussC では機能を「コアモジュール」と「アドオン」の2種類に分けて管理する。

### コアモジュール (tc:: 名前空間)

**特徴:**
- `#include <TrussC.h>` だけで使える
- 実装は `libTrussC` にコンパイル済み
- ユーザーは追加のビルド設定不要

**含まれる機能:**
- グラフィックス（描画、Image、FBO、Mesh 等）
- 数学ライブラリ（Vec、Mat、ノイズ、FFT 等）
- イベントシステム
- JSON / XML（nlohmann/json, pugixml）
- ImGui（Dear ImGui + sokol_imgui）
- Sound（sokol_audio + dr_libs）
- VideoGrabber（カメラ入力）
- FileDialog

**単一ヘッダーライブラリの扱い:**

stb、dr_libs、nlohmann/json など「単一ヘッダーライブラリ」は、`#define XXX_IMPLEMENTATION` が必要なものがある。これらは TrussC の実装ファイル（`src/*.cpp` / `src/*.mm`）で一度だけ展開し、`libTrussC` にコンパイルする。ユーザーはヘッダーをインクルードするだけで使える。

```
例: stb_image.h の場合

include/stb_image.h        ← ヘッダー（宣言のみ）
src/stb_impl.cpp           ← #define STB_IMAGE_IMPLEMENTATION + #include "stb_image.h"
```

**利点:**
- コンパイル時間の短縮（実装は1回だけコンパイル）
- リンクエラーの防止（多重定義を避ける）
- ユーザーは何も考えずに `#include` するだけでOK

### アドオン (tcx:: 名前空間)

**特徴:**
- 追加の CMake 設定が必要
- 外部依存ライブラリを使用する場合がある
- オプション機能や実験的機能

**将来の候補:**
- tcxOsc（Open Sound Control）
- tcxMidi（MIDI 入出力）
- tcxOpenCV（画像処理）
- tcxBox2D（物理演算）

**使い方:**

```cmake
# CMakeLists.txt
add_subdirectory(path/to/tcxOsc)
target_link_libraries(myApp PRIVATE tcx::Osc)
```

```cpp
// main.cpp
#include <TrussC.h>
#include <tcx/Osc.h>
```

### 判断基準

| 条件 | 分類 |
|------|------|
| 多くのプロジェクトで使う | コアモジュール |
| 外部依存なし or MIT互換 | コアモジュール |
| 特定用途向け | アドオン |
| 重い外部依存（OpenCV等） | アドオン |
| 実験的・不安定 | アドオン |
