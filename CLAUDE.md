# TrussC プロジェクト設定

## 概要
TrussC は sokol ベースの軽量クリエイティブコーディングフレームワーク。
openFrameworks に近いAPIを目指しつつ、モダンなC++でシンプルに実装する。

## フォルダ構造

```
trussc/                           # TrussC コアライブラリ
├── CMakeLists.txt                # メインCMake（GLOBベース）
├── include/
│   ├── TrussC.h                  # エントリポイント（全部インクルード）
│   ├── impl/                     # ライブラリ実装（stb, pugixml等）
│   ├── tc/
│   │   ├── app/                  # アプリケーション基盤
│   │   ├── graphics/             # 描画関連
│   │   ├── math/                 # 数学ユーティリティ
│   │   ├── types/                # 基本型（Color, Node等）
│   │   ├── events/               # イベントシステム
│   │   ├── utils/                # ユーティリティ
│   │   ├── 3d/                   # 3D機能
│   │   ├── gl/                   # 低レベルグラフィックス
│   │   ├── sound/                # サウンド（.h + .cpp）
│   │   ├── network/              # ネットワーク（.h + .cpp）
│   │   └── video/                # ビデオ
│   ├── sokol/                    # sokol ヘッダー
│   ├── imgui/                    # Dear ImGui
│   ├── stb/                      # stb ライブラリ
│   └── ...
├── platform/                     # プラットフォーム固有実装
│   ├── mac/                      # macOS (.mm)
│   ├── win/                      # Windows (.cpp)
│   └── linux/                    # Linux (.cpp)
├── addons/                       # オプショナルアドオン
│   └── tcxTls/                   # TLS/SSL サポート（mbedTLS）
│       ├── CMakeLists.txt
│       ├── include/
│       ├── src/
│       ├── libs/mbedtls/         # git submodule
│       └── examples/
└── resources/                    # リソース（アイコン等）

examples/                         # サンプルプロジェクト
├── templates/                    # テンプレート
├── graphics/                     # 描画サンプル
├── 3d/                           # 3Dサンプル
├── network/                      # ネットワークサンプル
├── tools/
│   └── projectGenerator/         # プロジェクト生成ツール
└── ...

projectGenerator/                 # ビルドスクリプト配布用
├── buildProjectGenerator_mac.command
├── buildProjectGenerator_win.bat
└── buildProjectGenerator_linux.sh
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
# サンプルのビルド
cd examples/graphics/colorExample
mkdir build && cd build
cmake ..
cmake --build .

# projectGenerator のビルド（macOS）
./projectGenerator/buildProjectGenerator_mac.command
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
