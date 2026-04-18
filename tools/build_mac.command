#!/bin/bash
# =============================================================================
# trusscli Build Script (macOS)
# =============================================================================
# Double-click this script to build trusscli (TrussC Project Generator GUI +
# the trusscli command-line tool — same binary, different entry points).
#
# NOTE: If macOS blocks this script, right-click and select "Open"
# =============================================================================

# Move to script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "  trusscli Build Script"
echo "=========================================="
echo ""

# Source directory (flat layout — CMakeLists.txt lives at SCRIPT_DIR)
SOURCE_DIR="$SCRIPT_DIR"

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
rm -rf "$SCRIPT_DIR/trusscli.app"
ln -s "$SOURCE_DIR/bin/trusscli.app" "$SCRIPT_DIR/trusscli.app"

# Install symlink to /usr/local/bin so trusscli is on PATH
# Skip if symlink already exists
if [ -L "/usr/local/bin/trusscli" ]; then
    echo "PATH symlink already installed."
    INSTALL_PATH_LINK="skip"
else
    echo ""
    read -r -p "Add trusscli to PATH via /usr/local/bin? [y/N]: " INSTALL_PATH_LINK
fi
case "$INSTALL_PATH_LINK" in
    [yY]|[yY][eE][sS])
        LINK_PATH="/usr/local/bin/trusscli"
        BIN_TARGET="$SOURCE_DIR/bin/trusscli.app/Contents/MacOS/trusscli"
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

# Enable shell tab completion
# Skip if already configured in shell rc file
SHELL_RC="$HOME/.zshrc"
SHELL_TYPE="zsh"
if [ -n "$BASH_VERSION" ]; then
    SHELL_RC="$HOME/.bash_profile"
    SHELL_TYPE="bash"
fi

if grep -q 'trusscli completion' "$SHELL_RC" 2>/dev/null; then
    echo "Shell completion already configured."
else
    echo ""
    read -r -p "Enable tab completion for trusscli? [y/N]: " INSTALL_COMPLETION
    case "$INSTALL_COMPLETION" in
        [yY]|[yY][eE][sS])
            echo "" >> "$SHELL_RC"
            echo '# TrussC CLI tab completion' >> "$SHELL_RC"
            echo "eval \"\$(trusscli completion $SHELL_TYPE)\"" >> "$SHELL_RC"
            echo "  Added to $SHELL_RC — restart your shell or run: source $SHELL_RC"
            ;;
        *)
            echo "Skipped. To enable later: eval \"\$(trusscli completion $SHELL_TYPE)\""
            ;;
    esac
fi

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""
echo "Launching TrussC Project Generator..."
open "$SCRIPT_DIR/trusscli.app"
