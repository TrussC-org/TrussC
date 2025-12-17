#!/bin/bash
# =============================================================================
# TrussC Project Generator ビルドスクリプト (macOS)
# =============================================================================
# このスクリプトをダブルクリックして projectGenerator をビルドできます
# =============================================================================

# スクリプトのあるディレクトリに移動
cd "$(dirname "$0")"

echo "=========================================="
echo "  TrussC Project Generator Build Script"
echo "=========================================="
echo ""

# projectGenerator ディレクトリに移動
cd projectGenerator

# build フォルダを作成
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

cd build

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

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""
echo "projectGenerator.app is located at:"
echo "  $(pwd)/../bin/projectGenerator.app"
echo ""

# アプリを開くか確認
read -p "Open projectGenerator now? (y/n): " answer
if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
    open ../bin/projectGenerator.app
fi
