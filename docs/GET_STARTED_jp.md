# TrussC をはじめよう

TrussC は openFrameworks にインスパイアされた軽量なクリエイティブコーディングフレームワークです。
C++20 + sokol で構築されており、シンプルに書けてクロスプラットフォームで動作します。

---

## AI アシスタント

<p align="center">
  <a href="https://gemini.google.com/gem/16aq3ccVPl33j6xSMyE7MWXDjaZvauL0I">
    <img src="images/TrussC_assistant.png" width="400" alt="TrussC Assistant">
  </a>
</p>

**[TrussC Assistant](https://gemini.google.com/gem/16aq3ccVPl33j6xSMyE7MWXDjaZvauL0I)** - TrussC を熟知した Gemini ベースの AI。API の質問、コード例の取得、トラブルシューティングに対応します。

---

## 1. 開発環境のセットアップ

### 必要なもの

| OS | コンパイラ |
|----|----------|
| macOS | Xcode Command Line Tools (`xcode-select --install`) |
| Windows | Visual Studio 2022 |
| Linux | GCC 10+ または Clang 10+ |

**CMake** も必要です:
```bash
# macOS
brew install cmake

# Windows
winget install Kitware.CMake

# Linux
sudo apt install cmake
```

### エディタのセットアップ

#### VSCode

| 拡張機能 | 用途 |
|-----------|---------|
| **CMake Tools** | ビルド連携 |
| **C/C++** | IntelliSense + デバッグ |
| **CodeLLDB** | デバッグ実行 (macOS / Linux) |

#### Cursor

| 拡張機能 | 用途 |
|-----------|---------|
| **CMake Tools** | ビルド連携 |
| **clangd** | IntelliSense (Cursor では C/C++ 拡張が使用不可) |
| **CodeLLDB** | デバッグ実行 (macOS / Linux) |

---

## 2. Project Generator のビルド

プロジェクト作成ツールをビルドします（初回のみ）。

**macOS:** `projectGenerator/buildProjectGenerator_mac.command` をダブルクリック

**Windows:** `projectGenerator/buildProjectGenerator_win.bat` をダブルクリック

---

## 3. プロジェクトの作成

<p align="center">
  <img src="images/projectGenerator_generate.png" width="500" alt="Project Generator">
</p>

1. **Project Name** にプロジェクト名を入力
2. **Location** に保存先を選択
3. **IDE** で `Cursor` または `VSCode` を選択
4. （オプション）**Web Build** にチェックで Emscripten ビルドスクリプトを生成
5. **Generate Project** をクリック

### Web ビルド (WebGPU)

TrussC は **WebGPU**（WebGL ではない）によるブラウザデプロイをサポートしています。Web 向けにビルドするには:

1. プロジェクト生成/更新時に **Web Build** にチェック
2. `build-web.command` (macOS) または `build-web.bat` (Windows) スクリプトが生成される
3. スクリプトを実行してビルドし、WebGPU 対応ブラウザで `bin/<プロジェクト名>.html` を開く

**VSCode/Cursor:** "Build Web" タスクも追加されます。`Cmd+Shift+B` / `Ctrl+Shift+B` → "Build Web" で実行できます。

> **注意:** WebGPU には最新のブラウザ（Chrome 113+、Edge 113+、Safari 18+）が必要です。

---

## 4. ビルドと実行

1. **Open in IDE** をクリックしてプロジェクトを開く
2. `F7` でビルド（または `Cmd+Shift+B` / `Ctrl+Shift+B`）
3. `F5` で実行

以上です！

---

## 5. サンプルを実行する

`examples/` フォルダに多くのサンプルがあります。

```
examples/
├── graphics/      # 2D 描画
├── 3d/            # 3D 描画
├── sound/         # サウンド
├── network/       # ネットワーク
├── gui/           # ImGui
└── ...
```

サンプルも同じ方法で実行できます:
1. Project Generator で **Import** をクリック
2. サンプルフォルダを選択
3. **Open in IDE** → `F5`

---

## 6. 既存プロジェクトの更新

<p align="center">
  <img src="images/projectGenerator_update.png" width="500" alt="Project Generator - Update">
</p>

TrussC プロジェクトやサンプル（例: `examples/graphics/graphicsExample`）をクローンした場合、ビルド前に IDE プロジェクトファイルを生成する必要があります。

1. **Import** をクリックしてプロジェクトフォルダを選択
2. **Update Project** をクリックして CMakeLists.txt と IDE ファイルを生成
3. **Open in IDE** → ビルドして実行

以下の場合にも便利です:
- ソースファイルのみを含むリポジトリをクローンした場合
- アドオンを変更した後にプロジェクトファイルを再生成したい場合
- IDE を切り替えたい場合（例: VSCode → Cursor）

---

## 7. コードを書く

### tcApp.h

```cpp
#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    // ライフサイクル
    void setup() override;      // 起動時に1回呼ばれる
    void update() override;     // 毎フレーム呼ばれる（ロジック）
    void draw() override;       // 毎フレーム呼ばれる（描画）
    void exit() override;       // cleanup の前に呼ばれる

    // キーイベント
    void keyPressed(int key) override;
    void keyReleased(int key) override;

    // マウスイベント
    void mousePressed(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void mouseMoved(Vec2 pos) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseScrolled(Vec2 delta) override;

    // ウィンドウイベント
    void windowResized(int width, int height) override;

    // ファイルドロップ
    void filesDropped(const vector<string>& files) override;
};
```

必要なメソッドだけをオーバーライドしてください。すべてのメソッドには空のデフォルト実装があります。

### tcApp.cpp

```cpp
#include "tcApp.h"

void tcApp::setup() {
    // 初期化
}

void tcApp::update() {
    // 毎フレーム呼ばれる
}

void tcApp::draw() {
    clear(30);  // 背景色

    setColor(colors::white);
    drawCircle(getWidth()/2, getHeight()/2, 100);
}

void tcApp::keyPressed(int key) {
    if (key == 'f') toggleFullscreen();
}

void tcApp::mousePressed(Vec2 pos, int button) {
    // マウスクリックの処理（pos.x, pos.y で座標を取得）
}

// ... 必要に応じて他のオーバーライドを実装
```

---

## 8. アプリアイコン

`icon/` フォルダに **512x512 以上の PNG** を配置します:

```
myProject/
├── icon/
│   └── myicon.png    ← PNG を置くだけ！
├── src/
└── CMakeLists.txt
```

PNG はビルド時に自動的に `.icns` (macOS) または `.ico` (Windows) に変換されます。

**Windows:** PNG → ICO 変換には [ImageMagick](https://imagemagick.org/) が必要です。または、`.ico` ファイルを直接用意してください。

