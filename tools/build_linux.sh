#!/bin/bash
# =============================================================================
# trusscli Build Script (Linux)
# =============================================================================
# Run this script to build trusscli (TrussC Project Generator GUI + the
# trusscli command-line tool — same binary, different entry points).
# Usage: ./build_linux.sh
# =============================================================================

# Move to script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "  trusscli Build Script"
echo "=========================================="
echo ""

# Check and install dependencies
"$SCRIPT_DIR/install_dependencies_linux.sh"
if [ $? -ne 0 ]; then
    echo "ERROR: Dependency check failed. Please install required packages."
    exit 1
fi
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
    echo "  Ubuntu/Debian: sudo apt install cmake"
    echo "  Fedora: sudo dnf install cmake"
    echo "  Arch: sudo pacman -S cmake"
    echo ""
    exit 1
fi

# Calculate parallel jobs based on available RAM (~1.5GiB per compile job)
AVAIL_KB=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
JOBS=$(awk "BEGIN { n = int($AVAIL_KB / 1572864); if (n < 1) n = 1; print n }")
MAX_JOBS=$(( $(nproc) + 2 ))
if [ "$JOBS" -gt "$MAX_JOBS" ]; then
    JOBS=$MAX_JOBS
fi

# Build
echo ""
echo "Building (parallel jobs: $JOBS)..."
cmake --build . --parallel "$JOBS"
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: Build failed!"
    echo ""
    exit 1
fi

# Create symlink to binary in distribution folder
echo ""
echo "Creating symlink to distribution folder..."
rm -f "$SCRIPT_DIR/trusscli"
ln -s "$SOURCE_DIR/bin/trusscli" "$SCRIPT_DIR/trusscli"

# Install desktop entry and icon (XDG user-level, no root required)
echo ""
read -r -p "Install desktop entry to application menu? [y/N]: " INSTALL_DESKTOP
case "$INSTALL_DESKTOP" in
    [yY]|[yY][eE][sS])
        echo "Installing desktop entry..."

        BIN_PATH="$SOURCE_DIR/bin/trusscli"
        ICON_SRC="$SOURCE_DIR/icon/generator.png"

        DESKTOP_DIR="$HOME/.local/share/applications"
        ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
        DESKTOP_FILE="$DESKTOP_DIR/trusscli.desktop"
        ICON_DEST="$ICON_DIR/trusscli.png"

        mkdir -p "$DESKTOP_DIR" "$ICON_DIR"

        if [ -f "$ICON_SRC" ]; then
            cp -f "$ICON_SRC" "$ICON_DEST"
        fi

        cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=TrussC Project Generator
GenericName=TrussC Project Generator
Comment=Generate TrussC projects
Exec="$BIN_PATH" %F
Path=$SOURCE_DIR/bin
Icon=trusscli
Terminal=false
Categories=Development;IDE;
StartupNotify=true
EOF

        chmod +x "$DESKTOP_FILE"

        # Refresh desktop database / icon cache if available
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
        fi

        echo "  Desktop entry: $DESKTOP_FILE"
        echo "  Icon:          $ICON_DEST"
        ;;
    *)
        echo "Skipped desktop entry installation."
        ;;
esac

# Install symlink to /usr/local/bin so trusscli is on PATH
echo ""
read -r -p "Add trusscli to PATH via /usr/local/bin? [y/N]: " INSTALL_PATH_LINK
case "$INSTALL_PATH_LINK" in
    [yY]|[yY][eE][sS])
        LINK_PATH="/usr/local/bin/trusscli"
        BIN_TARGET="$SOURCE_DIR/bin/trusscli"
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
echo "Launching TrussC Project Generator..."
"$SCRIPT_DIR/trusscli"
