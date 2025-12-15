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
