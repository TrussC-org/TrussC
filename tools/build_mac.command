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

# Defaults preserve interactive builds.
NO_DEPS=false
NON_INTERACTIVE=false
NO_LAUNCH=false
JOBS=""
usage() {
    cat <<EOF
Usage: $0 [options]
  --no-deps          Skip dependency installation (no-op on macOS)
  --jobs N           Use N parallel build jobs (positive integer)
  --non-interactive  Decline installation prompts and skip close waits
  --no-launch        Do not launch the GUI after building
  -h, --help         Show this help
EOF
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-deps) NO_DEPS=true ;;
        --non-interactive) NON_INTERACTIVE=true ;;
        --no-launch) NO_LAUNCH=true ;;
        --jobs)
            if [[ ! "${2:-}" =~ ^[0-9]+$ || ! "${2:-}" =~ [1-9] ]]; then
                echo "ERROR: --jobs requires a positive integer." >&2
                usage
                exit 1
            fi
            JOBS="$2"
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: Unknown option: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

if [ "$(uname)" != "Darwin" ]; then
    echo "ERROR: This script is for macOS. On Linux use tools/build_linux.sh; on Windows use tools/build_win.bat."
    if [ "$NON_INTERACTIVE" = false ]; then
        read -p "Press Enter to close..."
    fi
    exit 1
fi

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
# Pass configured launchers explicitly so restored CMake caches pick up CI's
# timing/cache launcher too (environment defaults apply only to a fresh cache).
LAUNCHER_ARGS=()
for lang in C CXX OBJC OBJCXX; do
    launcher_var="CMAKE_${lang}_COMPILER_LAUNCHER"
    if [ -n "${!launcher_var:-}" ]; then
        LAUNCHER_ARGS+=("-D${launcher_var}=${!launcher_var}")
    fi
done
echo "Running CMake..."
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel "${LAUNCHER_ARGS[@]}"
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: CMake configuration failed!"
    echo "Please make sure CMake is installed."
    echo "  brew install cmake"
    echo ""
    if [ "$NON_INTERACTIVE" = false ]; then
        read -p "Press Enter to close..."
    fi
    exit 1
fi

# Build
# Unix Makefiles treats a bare --parallel as make -j (unlimited). Match the
# machine's CPU count by default, while honoring CMake's explicit job override.
if [ -z "$JOBS" ]; then
    JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-$(sysctl -n hw.ncpu)}"
fi
echo ""
echo "Building (parallel jobs: $JOBS)..."
cmake --build . --config MinSizeRel --parallel "$JOBS"
if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: Build failed!"
    echo ""
    if [ "$NON_INTERACTIVE" = false ]; then
        read -p "Press Enter to close..."
    fi
    exit 1
fi

# Create symlink to binary in distribution folder
echo ""
echo "Creating symlink to distribution folder..."
rm -rf "$SCRIPT_DIR/trusscli.app"
ln -s "$SOURCE_DIR/bin/trusscli.app" "$SCRIPT_DIR/trusscli.app"

# Install symlink to /usr/local/bin so trusscli is on PATH
# Skip only if it already points to THIS binary
BIN_TARGET="$SOURCE_DIR/bin/trusscli.app/Contents/MacOS/trusscli"
CURRENT_LINK=$(readlink /usr/local/bin/trusscli 2>/dev/null)
if [ "$CURRENT_LINK" = "$BIN_TARGET" ]; then
    INSTALL_PATH_LINK="skip"
else
    echo ""
    if [ "$NON_INTERACTIVE" = false ]; then
        read -r -p "Add trusscli to PATH via /usr/local/bin? [y/N]: " INSTALL_PATH_LINK
    else
        INSTALL_PATH_LINK="n"
    fi
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
# Use $SHELL (user's login shell), not $BASH_VERSION (script interpreter)
if echo "$SHELL" | grep -q 'zsh'; then
    SHELL_RC="$HOME/.zshrc"
    SHELL_TYPE="zsh"
else
    SHELL_RC="$HOME/.bash_profile"
    SHELL_TYPE="bash"
fi

if grep -q 'trusscli completion' "$SHELL_RC" 2>/dev/null; then
    echo "Shell completion already configured."
else
    echo ""
    if [ "$NON_INTERACTIVE" = false ]; then
        read -r -p "Enable tab completion for trusscli? [y/N]: " INSTALL_COMPLETION
    else
        INSTALL_COMPLETION="n"
    fi
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
if [ "$NO_LAUNCH" = false ]; then
    echo "Launching TrussC Project Generator..."
    open "$SCRIPT_DIR/trusscli.app"
fi
