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
- [x] Path（頂点配列・曲線生成・ベジェ曲線・円弧）※旧 Polyline
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

### Addons（オプショナル）
- [x] tcxTls - TLS/SSL 通信（mbedTLS）
- [x] tcxOsc - OSC プロトコル
- [x] tcxBox2d - Box2D 物理エンジン

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

## 外部ライブラリの更新

TrussC はいくつかの外部ライブラリに依存している。
特に画像処理系は脆弱性が発見されやすいため、**リリースごとに最新版を確認する**。

| ライブラリ | 用途 | 更新優先度 | 備考 |
|:-----------|:-----|:-----------|:-----|
| **stb_image** | 画像読み込み | **高** | CVE多数、必ず最新版を使用 |
| **stb_image_write** | 画像書き出し | **高** | 同上 |
| **stb_truetype** | フォント描画 | 中 | |
| pugixml | XML パース | 中 | |
| nlohmann/json | JSON パース | 中 | |
| sokol | 描画バックエンド | 中 | API変更に注意、**TrussC カスタマイズあり（下記参照）** |
| miniaudio | オーディオ | 中 | |
| Dear ImGui | GUI | 低 | 安定版を使用 |

**更新時の確認事項:**
- GitHub の Release Notes / Security Advisories を確認
- stb は https://github.com/nothings/stb のコミット履歴を直接確認（タグがないため）

**sokol 更新時の注意（TrussC カスタマイズ）:**

`sokol_app.h` にはイベント駆動描画のちらつき防止のため、以下のカスタマイズが施されている。
sokol を更新する際は、これらの変更を再適用する必要がある。

1. `_sapp_t` 構造体に `bool skip_present;` フラグを追加
2. API宣言 `SOKOL_APP_API_DECL void sapp_skip_present(void);` を追加
3. `_sapp_d3d11_present()` の先頭にスキップチェックを追加
4. `sapp_skip_present()` の実装を追加

詳細は git diff で確認: `git log --oneline -p -- trussc/include/sokol/sokol_app.h`

---

## 既知の問題・課題

### Windows 固有

| 問題 | 説明 | 解決策案 |
|------|------|----------|
| ~~コンソールウィンドウ表示~~ | ~~実行ファイルをダブルクリックするとコマンドプロンプトが背後に表示される~~ | ✅ 解決済み: Release時は非表示、`TRUSSC_SHOW_CONSOLE` で表示可能 |
| ~~アイコン未適用~~ | ~~実行ファイルに .ico が適用されていない~~ | ✅ 解決済み: `trussc_setup_icon()` で .rc ファイル自動生成 |

### クロスプラットフォーム

| 問題 | 説明 | 解決策案 |
|------|------|----------|
| ~~イベントベース描画時のちらつき~~ | ~~redraw() 等で1フレームだけ更新すると、ダブルバッファの影響で直前の状態と交互に表示され点滅する~~ | ✅ 解決済み: sokol_app に `sapp_skip_present()` を追加し、描画スキップ時は Present もスキップ |

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

## プラットフォーム別テストチェックリスト

OS固有コードを含むため、重点的にテストが必要なサンプル・機能のリスト。

### Windows テスト項目

**🔴 最優先（OS固有コードが多い）**

| サンプル | 確認ポイント | 状況 |
|---------|-------------|------|
| network/tcpExample | Winsock 接続・切断・エラー処理 | ✅ 確認済み |
| network/udpExample | Winsock、ブロードキャスト、マルチキャスト | ✅ 確認済み |
| input_output/screenshotExample | D3D11 テクスチャキャプチャ (`tcPlatform_win.cpp`) | ✅ 確認済み（MSAA対応修正済み） |
| input_output/fileDialogExample | Win32 IFileDialog (`tcFileDialog_win.cpp`) | ✅ 確認済み |
| video/videoGrabberExample | Media Foundation API (`tcVideoGrabber_win.cpp`) | ⬜ 未確認（カメラ環境なし） |

**🟡 要確認（platform/win/ で実装あり）**

| 機能 | ファイル | 確認サンプル | 状況 |
|------|---------|-------------|------|
| FBO ピクセル読み取り | `tcFbo_win.cpp` | gl/fboExample | ✅ 確認済み |
| DPI スケール | `tcPlatform_win.cpp` | 全サンプル | ⬜ 未確認 |
| 実行ファイルパス | `tcPlatform_win.cpp` | dataPath 使用サンプル | ✅ 確認済み |
| コンソール UTF-8 | sokol_app.h | ログ出力全般 | ✅ 確認済み |

**🟢 比較的安全（クロスプラットフォームライブラリ）**

- graphics 系 - sokol が対応
- sound 系 - miniaudio が対応
- imgui 系 - sokol_imgui が対応

### Linux テスト項目

**🔴 未実装（要作成）**

| 機能 | ファイル | 必要な実装 | 状況 |
|------|---------|-----------|------|
| Platform 基本機能 | `tcPlatform_linux.cpp` | 現在スタブのみ、要実装 | ⬜ 未着手 |
| ├ getDisplayScaleFactor | | X11 `XRRGetScreenResources` | ⬜ |
| ├ setWindowSize | | X11 `XResizeWindow` | ⬜ |
| ├ getExecutablePath | | `/proc/self/exe` readlink | ⬜ |
| ├ captureWindow | | OpenGL `glReadPixels` | ⬜ |
| └ saveScreenshot | | stb_image_write | ⬜ |
| FBO ピクセル読み取り | `tcFbo_linux.cpp` | OpenGL `glReadPixels` | ⬜ 未着手 |
| VideoGrabber | `tcVideoGrabber_linux.cpp` | V4L2 | ⬜ 未着手 |

**🟡 要確認（POSIX コード）**

| サンプル | 確認ポイント | 状況 |
|---------|-------------|------|
| network/tcpExample | POSIX ソケット | ⬜ 未確認 |
| network/udpExample | POSIX ソケット | ⬜ 未確認 |
| communication/serialExample | POSIX termios | ⬜ 未確認 |
| input_output/fileDialogExample | GTK3 ダイアログ (`tcFileDialog_linux.cpp`) | ⬜ 未確認 |

**🟢 動作想定（クロスプラットフォーム）**

- graphics 系 - sokol OpenGL Core が対応
- sound 系 - miniaudio ALSA/PulseAudio 対応
- imgui 系 - sokol_imgui が対応

### Web (Emscripten) テスト項目

**✅ 基本動作確認済み**

| 機能 | 状況 |
|------|------|
| 描画（WebGL2） | ✅ 動作 |
| リサイズ | ✅ 動作（カスタムシェル使用） |
| フルスクリーン | ✅ 動作 |

**⬜ 未確認**

| 機能 | 備考 |
|------|------|
| キーボード入力 | |
| マウス入力 | |
| ImGui | |
| Sound (miniaudio) | WebAudio 対応が必要かも |
| Network | WebSocket 変換が必要 |

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
