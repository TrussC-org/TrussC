#!/bin/bash
# =============================================================================
# TrussC Project Generator ビルドスクリプト (Linux)
# =============================================================================
# このスクリプトを実行して projectGenerator をビルドできる
# 使い方: ./buildProjectGenerator_linux.sh
# =============================================================================

# スクリプトのあるディレクトリに移動
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "  TrussC Project Generator Build Script"
echo "=========================================="
echo ""

# ソースディレクトリ
SOURCE_DIR="$SCRIPT_DIR/../examples/tools/projectGenerator"

# build フォルダを作成
if [ ! -d "$SOURCE_DIR/build" ]; then
    echo "Creating build directory..."
    mkdir -p "$SOURCE_DIR/build"
fi

cd "$SOURCE_DIR/build"

# CMake 設定
echo "Running CMake..."
cmake ..
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: CMake configuration failed!"
    echo "Please make sure CMake is installed."
    echo "  Ubuntu/Debian: sudo apt install cmake"
    echo "  Fedora: sudo dnf install cmake"
    echo "  Arch: sudo pacman -S cmake"
    echo ""
    exit 1
fi

# ビルド
echo ""
echo "Building..."
cmake --build .
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: Build failed!"
    echo ""
    exit 1
fi

# バイナリをコピー
echo ""
echo "Copying to distribution folder..."
cp "$SOURCE_DIR/bin/projectGenerator" "$SCRIPT_DIR/"

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""
echo "projectGenerator is located at:"
echo "  $SCRIPT_DIR/projectGenerator"
echo ""

# アプリを実行するか確認
read -p "Run projectGenerator now? (y/n): " answer
if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
    "$SCRIPT_DIR/projectGenerator"
fi
