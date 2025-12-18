# TrussC ロードマップ

openFrameworksとの機能比較に基づいた開発ロードマップ。

---

## 実装済み機能

### Graphics（描画）
- [x] 基本図形（rect, circle, ellipse, line, triangle, point）
- [x] Image（読み込み・描画・保存）
- [x] BitmapFont（テクスチャアトラス）
- [x] TrueTypeFont（stb_truetype ベース）
- [x] Shape API（beginShape/vertex/endShape）
- [x] Polyline（頂点配列・曲線生成）
- [x] Mesh（頂点・色・インデックス・法線）
- [x] StrokeMesh（太線描画）
- [x] Scissor Clipping（再帰対応）
- [x] ブレンドモード（Alpha, Add, Multiply, Screen, Subtract, Disabled）

### 3D
- [x] 3D変形（translate/rotate/scale）
- [x] 深度テスト・背面カリング
- [x] 3Dプリミティブ（Plane, Box, Sphere, Cylinder, Cone, Torus, IcoSphere）
- [x] EasyCam（マウス操作3Dカメラ）
- [x] Node（シーングラフ）
- [x] RectNode（2D UI、Ray-based Hit Test）
- [x] ライティング（Ambient, Diffuse, Specular / Phong モデル）
- [x] マテリアル（プリセット: gold, silver, copper, emerald 等）
- [x] Light（Directional, Point）

### Math
- [x] Vec2, Vec3, Vec4
- [x] Mat3, Mat4
- [x] Perlin Noise（1D〜4D）
- [x] Ray（Hit Test用）
- [x] 色空間（RGB, HSB, OKLab, OKLCH）
- [x] ユーティリティ（lerp, clamp, map, radians, degrees）

### Events
- [x] キーボード（keyPressed, keyReleased）
- [x] マウス（pressed, released, moved, dragged, scrolled）
- [x] ウィンドウリサイズ
- [x] Event<T> テンプレート
- [x] EventListener（RAII）
- [x] RectNode イベント（mousePressed/Released/Dragged/Scrolled）
- [x] ドラッグ&ドロップ（ファイル受け取り）

### GL
- [x] Shader（フルスクリーンシェーダー）
- [x] FBO（フレームバッファオブジェクト）
- [x] テクスチャ詳細制御（Filter: Nearest/Linear、Wrap: Repeat/MirroredRepeat/ClampToEdge）

### Utils
- [x] Timer（Node::addTimerFunction）
- [x] Thread, ThreadChannel
- [x] Serial通信
- [x] フレーム制御（FPS/VSync）
- [x] Log（tcLog: Verbose/Notice/Warning/Error/Fatal）
- [x] JSON / XML（nlohmann/json, pugixml）
- [x] ファイルダイアログ（OS標準ダイアログ）
- [x] ネットワーク（TCP/UDP）

### Sound
- [x] Sound（sokol_audio + dr_libs）
- [x] SoundStream（オーディオ入力）

### Video
- [x] VideoGrabber（Webカメラ入力）

### UI
- [x] Dear ImGui 統合

---

## 未実装機能（優先度別）

### 優先度: 中

| 機能 | 説明 | 難易度 |
|------|------|--------|
| パス描画（曲線） | ベジェ曲線、円弧 | 中 |
| 3Dモデル読み込み | obj/gltf | 高 |
| テクスチャマッピング | Mesh へのテクスチャ適用 | 中 |
| 法線マップ | バンプマッピング | 高 |
| ビデオ再生 | 動画ファイル再生 | 高 |

### 優先度: 低

| 機能 | 説明 | 難易度 |
|------|------|--------|
| VBO詳細制御 | 動的頂点バッファ | 中 |
| パーティクルシステム | アドオン化も検討 | 中 |
| タッチ入力 | iOS/Android向け | 高 |
| Spot ライト | スポットライト対応 | 中 |

---

## 既知の問題・課題

### Windows 固有

| 問題 | 説明 | 解決策案 |
|------|------|----------|
| コンソールウィンドウ表示 | 実行ファイルをダブルクリックするとコマンドプロンプトが背後に表示される | `#pragma comment(linker, "/SUBSYSTEM:WINDOWS")` + `WinMain` 対応。デフォルトで非表示、マクロ定義で表示可能にする |
| アイコン未適用 | 実行ファイルに .ico が適用されていない | Windows リソースファイル (.rc) の設定が必要 |

### クロスプラットフォーム

| 問題 | 説明 | 解決策案 |
|------|------|----------|
| イベントベース描画時のちらつき | redraw() 等で1フレームだけ更新すると、ダブルバッファの影響で直前の状態と交互に表示され点滅する | 2フレーム連続で描画するか、フロントバッファ/バックバッファの同期を検討 |

---

## Windows / Linux 対応状況

現在 macOS (Metal) で開発中。Windows / Linux への移植作業の状況。

### ✅ 対応済み（クロスプラットフォーム）

| 機能 | 実装 | 備考 |
|:-----|:-----|:-----|
| **Core (sokol)** | sokol_app / sokol_gfx | Metal / D3D11 / OpenGL / Vulkan / WebGPU |
| **FileDialog** | mac.mm / win.cpp / linux.cpp | OS標準ダイアログ |
| **UDP Socket** | `#ifdef` 分岐 | Winsock / POSIX 両対応 |
| **TCP Client/Server** | `#ifdef` 分岐 | Winsock / POSIX 両対応 |
| **Sound** | miniaudio | クロスプラットフォーム |
| **ImGui** | - | クロスプラットフォーム |
| **Serial** | `tcSerial.h` | Win32 API / POSIX 両対応 |
| **Platform** | `tcPlatform_win.cpp` | getExecutablePath, setWindowSize, getDisplayScaleFactor, captureWindow, saveScreenshot |
| **FBO** | `tcFbo_win.cpp` | D3D11 Map/Unmap でピクセル読み取り |
| **VideoGrabber** | `tcVideoGrabber_win.cpp` | Media Foundation でウェブカメラ対応 |

### ❌ macOS のみ → Linux 実装が必要

| 機能 | ファイル | Linux |
|:-----|:---------|:------|
| **Platform** | `tcPlatform_linux.cpp` | |
| ├ getDisplayScaleFactor | | X11: `XRRGetScreenResources` |
| ├ setWindowSize | | X11: `XResizeWindow` |
| ├ getExecutablePath | | `/proc/self/exe` |
| ├ captureWindow | | OpenGL `glReadPixels` |
| └ saveScreenshot | | stb_image_write で代用可 |
| **FBO** | `tcFbo_linux.cpp` | OpenGL `glReadPixels` |
| **VideoGrabber** | `tcVideoGrabber_linux.cpp` | V4L2 |

### 移植の優先度（Linux）

**中（使う人は使う）:**
1. `tcFbo_linux.cpp` - FBO ピクセル読み取り
2. `tcVideoGrabber_linux.cpp` - カメラ入力
3. `tcPlatform_linux.cpp` - プラットフォーム関数

---

## サンプル一覧

### 実装済み

| カテゴリ | サンプル |
|---------|---------|
| templates/ | emptyExample |
| graphics/ | graphicsExample, colorExample, clippingExample, blendingExample, fontExample, polylinesExample, strokeMeshExample |
| 3d/ | ofNodeExample, 3DPrimitivesExample（ライティング込み）, easyCamExample |
| math/ | vectorMathExample, noiseField2dExample |
| events/ | eventsExample, hitTestExample, uiExample |
| gl/ | shaderExample, textureExample |
| input_output/ | fileDialogExample, imageLoaderExample, screenshotExample, dragDropExample, jsonXmlExample, keyboardExample, mouseExample |
| sound/ | soundPlayerExample, soundPlayerFFTExample, micInputExample |
| video/ | videoGrabberExample |
| network/ | tcpExample, udpExample |
| communication/ | serialExample |
| gui/ | imguiExample |
| threads/ | threadExample, threadChannelExample |
| windowing/ | loopModeExample |

### 今後

| カテゴリ | サンプル | 優先度 |
|---------|---------|--------|
| 3d/ | modelLoaderExample | 中 |

---

## 参考リンク

- [oF Examples](https://github.com/openframeworks/openFrameworks/tree/master/examples)
- [oF Documentation](https://openframeworks.cc/documentation/)
- [sokol](https://github.com/floooh/sokol)
