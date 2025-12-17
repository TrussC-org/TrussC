# TrussC ビルド方法

## 前提条件

- CMake 3.20 以上
- C++20 対応コンパイラ
- macOS: Xcode Command Line Tools (`xcode-select --install`)
- Windows: Visual Studio 2022 または MinGW

---

## 1. コマンドライン（cmake 直打ち）

最もシンプルな方法。どのプラットフォームでも動く。

```bash
# サンプルのフォルダに移動
cd examples/graphics/graphicsExample

# build フォルダを作成して移動
mkdir -p build && cd build

# CMake で構成
cmake ..

# ビルド
cmake --build .

# 実行（macOS）
open ./graphicsExample.app

# 実行（Windows）
./graphicsExample.exe
```

### リリースビルド

```bash
cmake --build . --config Release
```

### クリーンビルド

```bash
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```

---

## 2. VSCode

### 準備

拡張機能をインストール：
```bash
code --install-extension ms-vscode.cmake-tools
code --install-extension ms-vscode.cpptools
```

または VSCode 内で `Cmd+Shift+X`（macOS）/ `Ctrl+Shift+X`（Windows）から検索してインストール。

### 使い方

1. サンプルフォルダを VSCode で開く
   ```bash
   code examples/graphics/graphicsExample
   ```

2. CMake Tools が自動で `CMakeLists.txt` を検出

3. コンパイラを選択（初回のみ）
   - `Cmd+Shift+P` → `CMake: Select a Kit`

4. ビルド
   - `Cmd+Shift+P` → `CMake: Build`
   - または `F7`

5. 実行
   - `Cmd+Shift+P` → `CMake: Run Without Debugging`
   - または下部ステータスバーの再生ボタン

---

## 3. Xcode（macOS）

```bash
cd examples/graphics/graphicsExample
mkdir -p build && cd build

# Xcode プロジェクトを生成
cmake -G Xcode ..

# Xcode で開く
open *.xcodeproj
```

Xcode 内で `Cmd+R` で実行。

---

## 4. Visual Studio（Windows）

```bash
cd examples\graphics\graphicsExample
mkdir build && cd build

# Visual Studio プロジェクトを生成
cmake -G "Visual Studio 17 2022" ..

# Visual Studio で開く
start *.sln
```

Visual Studio 内で `F5` で実行。

---

## 5. CLion

1. CLion でサンプルフォルダを開く
2. 自動で CMake を認識
3. 右上の再生ボタンでビルド & 実行

---

## 全サンプル一括ビルド

プロジェクトルートにビルドスクリプトがある場合：

```bash
./build_all_examples.sh
```

---

## トラブルシューティング

### CMake が見つからない

```bash
# macOS (Homebrew)
brew install cmake

# Windows (winget)
winget install Kitware.CMake
```

### コンパイラが見つからない（macOS）

```bash
xcode-select --install
```

### ビルドエラーが出る

```bash
# build フォルダを削除してやり直し
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```
