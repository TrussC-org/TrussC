# TrussC

sokol ベースの軽量クリエイティブコーディングフレームワーク。
openFrameworks に近い API を目指しつつ、モダンな C++ でシンプルに実装。

## 特徴

- **軽量**: sokol ライブラリをベースに、最小限の依存関係
- **ヘッダーオンリー**: ほとんどの機能がヘッダーファイルのみで完結
- **C++20**: モダンな C++ 機能を活用
- **クロスプラットフォーム**: macOS (Metal)、将来的に Windows/Linux 対応予定
- **oF ライク API**: openFrameworks ユーザーが馴染みやすい設計

## クイックスタート

```bash
# サンプルをビルド
cd examples/graphics/graphicsExample
mkdir build && cd build
cmake ..
cmake --build .

# 実行（macOS）
./bin/graphicsExample.app/Contents/MacOS/graphicsExample
```

詳細なビルド手順は [docs/HOW_TO_BUILD.md](docs/HOW_TO_BUILD.md) を参照。

### 最小限のコード

```cpp
#include "TrussC.h"
#include "tcBaseApp.h"

class MyApp : public tc::App {
public:
    void setup() override {
        // 初期化処理
    }

    void draw() override {
        tc::clear(30);  // 背景をグレーでクリア

        tc::setColor(255, 100, 100);
        tc::drawCircle(400, 300, 100);  // 赤い円を描画
    }
};

int main() {
    tc::runApp<MyApp>({
        .width = 800,
        .height = 600,
        .title = "My App"
    });
    return 0;
}
```

## サンプル一覧

### templates/
- **emptyExample** - 最小構成テンプレート

### graphics/
- **graphicsExample** - 基本図形描画
- **colorExample** - 色空間と補間

### 3d/
- **ofNodeExample** - シーングラフ・ノードシステム
- **3DPrimitivesExample** - 3D プリミティブ
- **easyCamExample** - マウス操作 3D カメラ

### math/
- **vectorMathExample** - ベクトル・行列演算

### input_output/
- **imageLoaderExample** - 画像読み込み・描画
- **screenshotExample** - FBO を使ったスクリーンショット

### events/
- **keyPressedExample** - キーボードイベント
- **mouseExample** - マウスイベント
- **allEventsExample** - 全イベント一覧

### threads/
- **threadExample** - マルチスレッド処理

## 主な API

### 描画

```cpp
tc::clear(r, g, b);              // 背景クリア
tc::setColor(r, g, b, a);        // 描画色設定
tc::drawRect(x, y, w, h);        // 矩形
tc::drawCircle(x, y, radius);    // 円
tc::drawLine(x1, y1, x2, y2);    // 線
tc::drawTriangle(x1, y1, ...);   // 三角形
```

### 変形

```cpp
tc::translate(x, y);
tc::rotate(radians);
tc::scale(sx, sy);
tc::pushMatrix();
tc::popMatrix();
```

### 入力

```cpp
tc::getMouseX();
tc::getMouseY();
tc::isMousePressed();
```

### 時間

```cpp
tc::getElapsedTime();    // 経過秒数
tc::getDeltaTime();      // フレーム間隔
tc::getFrameRate();      // FPS
```

### 色

```cpp
tc::Color c(1.0f, 0.5f, 0.0f);           // RGB
tc::colorFromHSB(hue, sat, brightness);  // HSB から生成
tc::colors::cornflowerBlue;              // 定義済みカラー
```

## 依存関係

- [sokol](https://github.com/floooh/sokol) - クロスプラットフォーム描画
- [stb](https://github.com/nothings/stb) - 画像読み込み (stb_image)

すべて `include/` 以下に同梱済み。

## ディレクトリ構成

```
include/
├── TrussC.h          # メインヘッダー（これを include）
├── tcBaseApp.h       # App 基底クラス
├── tc/               # 機能別ヘッダー
│   ├── graphics/     # 描画関連
│   ├── math/         # 数学ユーティリティ
│   ├── types/        # Color, Vec2 など
│   ├── events/       # イベントシステム
│   ├── 3d/           # 3D 機能
│   └── gl/           # FBO, シェーダーなど
├── sokol/            # sokol ライブラリ
└── stb/              # stb ライブラリ

examples/             # サンプル集
src/                  # プラットフォーム固有実装
```

## ドキュメント

- [HOW_TO_BUILD.md](docs/HOW_TO_BUILD.md) - ビルド方法
- [DESIGN.md](docs/DESIGN.md) - 設計詳細（Loop Architecture, 3D Projection）
- [PHILOSOPHY.md](docs/PHILOSOPHY.md) - コンセプト・哲学・技術スタック
- [ROADMAP.md](docs/ROADMAP.md) - 実装ロードマップ
- [TrussC_vs_openFrameworks.md](docs/TrussC_vs_openFrameworks.md) - oF ユーザー向けガイド・API対応表

## ライセンス

MIT License

## 参考

- [openFrameworks](https://openframeworks.cc/)
- [sokol](https://github.com/floooh/sokol)
