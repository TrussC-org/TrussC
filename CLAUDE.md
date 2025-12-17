# TrussC プロジェクト設定

## 概要
TrussC は sokol ベースの軽量クリエイティブコーディングフレームワーク。
openFrameworks に近いAPIを目指しつつ、モダンなC++でシンプルに実装する。

## フォルダ構造

```
include/
├── TrussC.h                  # エントリポイント（全部インクルード）
├── tc/
│   ├── app/
│   │   ├── tcRunner.h            # runApp, WindowSettings
│   │   ├── tcWindow.h            # ウィンドウ制御・情報
│   │   └── tcBaseApp.h           # App基底クラス
│   │
│   ├── graphics/
│   │   ├── tcGraphics.h          # 図形描画（rect, circle, line等）
│   │   ├── tcBitmapFont.h        # ビットマップフォント
│   │   ├── tcStyle.h             # 色設定、fill/stroke
│   │   ├── tcImage.h             # [将来] 画像読み込み・描画
│   │   ├── tcPath.h              # [将来] パス描画
│   │   ├── tcPixels.h            # [将来] ピクセル操作
│   │   └── tcTrueTypeFont.h      # [将来] TTFフォント
│   │
│   ├── math/
│   │   ├── tcMath.h              # 数学ユーティリティ（lerp, map, clamp等）
│   │   ├── tcVec.h               # [将来] Vec2/Vec3/Vec4（tcMathから分離）
│   │   ├── tcMatrix.h            # [将来] Mat3/Mat4（tcMathから分離）
│   │   └── tcTransform.h         # 変形（translate/rotate/scale等）
│   │
│   ├── types/
│   │   ├── tcColor.h             # Color構造体、色空間（HSB/OKLab/OKLCH）
│   │   ├── tcNode.h              # Node（シーングラフ）
│   │   └── tcRectangle.h         # [将来] Rectangle型
│   │
│   ├── platform/
│   │   └── tcPlatform.h          # プラットフォーム抽象化
│   │
│   ├── utils/
│   │   ├── tcTime.h              # 時間関連（getElapsedTime, getDeltaTime等）
│   │   ├── tcInput.h             # マウス・キーボード入力
│   │   ├── tcTimer.h             # [将来] タイマー（callAfter, callEvery）
│   │   └── tcFile.h              # [将来] ファイル操作
│   │
│   ├── events/                   # イベントシステム
│   │   ├── tcEvent.h             # Event<T> テンプレートクラス
│   │   ├── tcEventListener.h     # EventListener（RAII トークン）
│   │   ├── tcEventArgs.h         # イベント引数構造体群
│   │   └── tcCoreEvents.h        # CoreEvents + events() アクセサ
│   │
│   ├── 3d/                       # [将来] 3D機能
│   │   ├── tc3dGraphics.h        # 3D描画
│   │   ├── tcCamera.h            # カメラ
│   │   ├── tcMesh.h              # メッシュ
│   │   └── tcLight.h             # ライティング
│   │
│   ├── gl/                       # [将来] 低レベルグラフィックス
│   │   ├── tcShader.h            # シェーダー
│   │   ├── tcFbo.h               # フレームバッファオブジェクト
│   │   ├── tcTexture.h           # テクスチャ
│   │   └── tcVbo.h               # 頂点バッファ
│   │
│   ├── sound/                    # [将来] サウンド
│   │   ├── tcSound.h             # サウンド再生
│   │   └── tcSoundStream.h       # オーディオストリーム
│   │
│   └── video/                    # [将来] ビデオ
│       ├── tcVideo.h             # ビデオ再生
│       └── tcVideoGrabber.h      # カメラキャプチャ
│
├── sokol/                        # 外部ライブラリ（そのまま）
│   ├── sokol_app.h
│   ├── sokol_gfx.h
│   ├── sokol_gl.h
│   └── ...
│
src/
├── sokol_impl.mm                 # sokol実装（macOS）
├── sokol_impl.cpp                # sokol実装（他プラットフォーム）
└── tcPlatform_mac.mm             # macOS固有実装

examples/                             # oFと同じフォルダ構造
├── templates/
│   └── emptyExample/             # 最小構成テンプレート
├── graphics/
│   ├── graphicsExample/          # 図形描画
│   └── colorExample/             # 色空間・補間
├── 3d/
│   ├── ofNodeExample/            # シーングラフ
│   └── 3DPrimitivesExample/      # 3Dプリミティブ
└── math/
    └── vectorMathExample/        # 数学ライブラリ
```

## 命名規則

- ファイル名: `tcXxx.h` （小文字tc + PascalCase）
- 名前空間: `trussc::` (エイリアス `tc::`)
- クラス/構造体: PascalCase（例: `Color`, `WindowSettings`）
- 関数: camelCase（例: `drawRect`, `setColor`）
- 定数: SCREAMING_SNAKE_CASE または camelCase（数学定数は `TAU`, `PI` など）
- 色定数: `tc::colors::camelCase`（例: `tc::colors::cornflowerBlue`）

## コーディングスタイル

- C++20を使用
- ヘッダーオンリー（inline関数）を基本とする
- sokol の API をラップして使いやすくする
- openFrameworks に近いAPIを目指す
- `std::` プレフィックスは省略（using namespace std 前提）

## ビルド

```bash
cd examples/graphics/colorExample/build
cmake ..
cmake --build .
```

## 関連ドキュメント

- [docs/HOW_TO_BUILD.md](docs/HOW_TO_BUILD.md) - ビルド方法
- [docs/DESIGN.md](docs/DESIGN.md) - 設計詳細（Loop Architecture, 3D Projection等）
- [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) - コンセプト・哲学・技術スタック
- [docs/ROADMAP.md](docs/ROADMAP.md) - 実装ロードマップ
- [docs/TrussC_vs_openFrameworks.md](docs/TrussC_vs_openFrameworks.md) - oF ユーザー向けガイド

## 参考

- openFrameworks: https://openframeworks.cc/
- sokol: https://github.com/floooh/sokol
