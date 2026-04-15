#!/bin/bash
# =============================================================================
# TrussC Project Generator Build Script (macOS)
# =============================================================================
# Double-click this script to build projectGenerator
#
# NOTE: If macOS blocks this script, right-click and select "Open"
# =============================================================================

# Move to script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "  TrussC Project Generator Build Script"
echo "=========================================="
echo ""

# Source directory
SOURCE_DIR="$SCRIPT_DIR/tools/projectGenerator"

# Create build folder
if [ ! -d "$SOURCE_DIR/build" ]; then
    echo "Creating build directory..."
    mkdir -p "$SOURCE_DIR/build"
fi

cd "$SOURCE_DIR/build"

# CMake configuration
echo "Running CMake..."
cmake ..
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: CMake configuration failed!"
    echo "Please make sure CMake is installed."
    echo "  brew install cmake"
    echo ""
    read -p "Press Enter to close..."
    exit 1
fi

# Build
echo ""
echo "Building..."
cmake --build . --parallel
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: Build failed!"
    echo ""
    read -p "Press Enter to close..."
    exit 1
fi

# Create symlink to binary in distribution folder
echo ""
echo "Creating symlink to distribution folder..."
rm -rf "$SCRIPT_DIR/projectGenerator.app"
ln -s "$SOURCE_DIR/bin/projectGenerator.app" "$SCRIPT_DIR/projectGenerator.app"

# Install symlink to /usr/local/bin so projectGenerator is on PATH
echo ""
read -r -p "Add projectGenerator to PATH via /usr/local/bin? [y/N]: " INSTALL_PATH_LINK
case "$INSTALL_PATH_LINK" in
    [yY]|[yY][eE][sS])
        LINK_PATH="/usr/local/bin/projectGenerator"
        BIN_TARGET="$SOURCE_DIR/bin/projectGenerator.app/Contents/MacOS/projectGenerator"
        # /usr/local/bin may not exist on a clean Apple Silicon install
        if [ ! -d "/usr/local/bin" ]; then
            echo "  /usr/local/bin does not exist — creating it (requires sudo)."
            sudo mkdir -p /usr/local/bin
        fi
        if [ -w "/usr/local/bin" ]; then
            ln -sf "$BIN_TARGET" "$LINK_PATH"
        else
            echo "  /usr/local/bin requires sudo — you may be prompted for your password."
            sudo ln -sf "$BIN_TARGET" "$LINK_PATH"
        fi
        if [ $? -eq 0 ]; then
            echo "  Symlink: $LINK_PATH -> $BIN_TARGET"
        else
            echo "  ERROR: Failed to create symlink."
        fi
        ;;
    *)
        echo "Skipped PATH symlink installation."
        ;;
esac

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""
echo "Launching projectGenerator..."
open "$SCRIPT_DIR/projectGenerator.app"
