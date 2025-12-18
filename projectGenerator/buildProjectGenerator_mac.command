#!/bin/bash
# =============================================================================
# TrussC Project Generator ビルドスクリプト (macOS)
# =============================================================================
# このスクリプトをダブルクリックして projectGenerator をビルドできます
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
    echo ""
    read -p "Press Enter to close..."
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
    read -p "Press Enter to close..."
    exit 1
fi

# バイナリをコピー
echo ""
echo "Copying to distribution folder..."
rm -rf "$SCRIPT_DIR/projectGenerator.app"
cp -R "$SOURCE_DIR/bin/projectGenerator.app" "$SCRIPT_DIR/"

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""
echo "projectGenerator.app is located at:"
echo "  $SCRIPT_DIR/projectGenerator.app"
echo ""

# アプリを開くか確認
read -p "Open projectGenerator now? (y/n): " answer
if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
    open "$SCRIPT_DIR/projectGenerator.app"
fi
