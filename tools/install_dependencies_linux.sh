#!/bin/bash
# =============================================================================
# TrussC Linux Dependency Installer
# =============================================================================
# Checks for required packages and installs missing ones.
# Supports Debian/Ubuntu (apt) and Arch Linux (pacman).
# Usage:
#   ./install_dependencies_linux.sh       # Interactive mode (asks before install)
#   ./install_dependencies_linux.sh -y    # Auto-install without asking
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Detect distribution
DISTRO=""
if [ -r /etc/os-release ]; then
    . /etc/os-release
    DISTRO="$ID"
    DISTRO_LIKE="$ID_LIKE"
fi

# Required packages per distribution (keep in sync — these are the single source of truth)
REQUIRED_PACKAGES_DEBIAN=(
    libx11-dev
    libxcursor-dev
    libxi-dev
    libxrandr-dev
    libgl1-mesa-dev
    libasound2-dev
    libgtk-3-dev
    libavcodec-dev
    libavformat-dev
    libswscale-dev
    libavutil-dev
    libgstreamer1.0-dev
    libgstreamer-plugins-base1.0-dev
    gstreamer1.0-plugins-good
    gstreamer1.0-plugins-bad
    pkg-config
    libcurl4-openssl-dev
    cmake
)

REQUIRED_PACKAGES_ARCH=(
    libx11
    libxcursor
    libxi
    libxrandr
    mesa
    alsa-lib
    gtk3
    ffmpeg
    gstreamer
    gst-plugins-base
    gst-plugins-good
    gst-plugins-bad
    pkgconf
    curl
    cmake
)

AUTO_YES=false
if [ "$1" = "-y" ]; then
    AUTO_YES=true
fi

# Pick package manager based on distro
PKG_MANAGER=""
case "$DISTRO" in
    ubuntu|debian|linuxmint|pop|elementary|kali|raspbian)
        PKG_MANAGER="apt"
        ;;
    arch|manjaro|endeavouros|artix|cachyos)
        PKG_MANAGER="pacman"
        ;;
    *)
        # Fallback: check ID_LIKE
        case "$DISTRO_LIKE" in
            *debian*|*ubuntu*) PKG_MANAGER="apt" ;;
            *arch*)            PKG_MANAGER="pacman" ;;
        esac
        ;;
esac

if [ -z "$PKG_MANAGER" ]; then
    echo "ERROR: Unsupported Linux distribution: ${DISTRO:-unknown}"
    echo "Supported: Debian/Ubuntu (apt), Arch (pacman)"
    echo "Please install the required dependencies manually."
    exit 1
fi

# Build the missing-package list and prepare install commands
MISSING=()
case "$PKG_MANAGER" in
    apt)
        REQUIRED_PACKAGES=("${REQUIRED_PACKAGES_DEBIAN[@]}")
        for pkg in "${REQUIRED_PACKAGES[@]}"; do
            if ! dpkg -s "$pkg" &>/dev/null; then
                MISSING+=("$pkg")
            fi
        done
        MANUAL_INSTALL_CMD="sudo apt-get install"
        ;;
    pacman)
        REQUIRED_PACKAGES=("${REQUIRED_PACKAGES_ARCH[@]}")
        for pkg in "${REQUIRED_PACKAGES[@]}"; do
            if ! pacman -Qi "$pkg" &>/dev/null; then
                MISSING+=("$pkg")
            fi
        done
        MANUAL_INSTALL_CMD="sudo pacman -S"
        ;;
esac

if [ ${#MISSING[@]} -eq 0 ]; then
    echo "All required packages are already installed."
    exit 0
fi

echo "Detected distribution: $DISTRO ($PKG_MANAGER)"
echo "The following packages are missing:"
echo ""
for pkg in "${MISSING[@]}"; do
    echo "  - $pkg"
done
echo ""

if [ "$AUTO_YES" = true ]; then
    echo "Installing (auto-yes mode)..."
else
    read -p "Install now? [Y/n] " answer
    case "$answer" in
        [nN]*)
            echo "Skipped. You can install manually with:"
            echo "  $MANUAL_INSTALL_CMD ${MISSING[*]}"
            exit 1
            ;;
    esac
fi

case "$PKG_MANAGER" in
    apt)
        sudo apt-get update
        sudo apt-get install -y "${MISSING[@]}"
        ;;
    pacman)
        sudo pacman -Sy --needed --noconfirm "${MISSING[@]}"
        ;;
esac

if [ $? -ne 0 ]; then
    echo "ERROR: Failed to install some packages."
    exit 1
fi

echo "All dependencies installed successfully."
