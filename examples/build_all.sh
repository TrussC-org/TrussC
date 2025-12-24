#!/bin/bash
# =============================================================================
# build_all.sh - Build script for all examples using CMake Presets
# =============================================================================
# Usage:
#   ./build_all.sh          # Build all examples
#   ./build_all.sh --clean  # Clean build (delete build directories)
#   ./build_all.sh --web    # Build for WebAssembly
#   ./build_all.sh --help   # Show help

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLEAN_BUILD=false
VERBOSE=false
WEB_BUILD=false

# Colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect platform
detect_preset() {
    if [ "$WEB_BUILD" = true ]; then
        echo "web"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "linux"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "msys"* ]] || [[ "$OSTYPE" == "cygwin"* ]]; then
        echo "windows"
    else
        echo "linux"  # fallback
    fi
}

# Show help
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --clean    Clean build (delete build directories and rebuild)"
    echo "  --verbose  Show detailed build output"
    echo "  --web      Build for WebAssembly using Emscripten"
    echo "  --help     Show this help"
    echo ""
    echo "Examples:"
    echo "  $0              # Build all examples for current platform"
    echo "  $0 --clean      # Clean build"
    echo "  $0 --web        # Build for WebAssembly"
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --web)
            WEB_BUILD=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

PRESET=$(detect_preset)

echo -e "${BLUE}=== TrussC Examples Build Script ===${NC}"
echo -e "${YELLOW}Preset: $PRESET${NC}"
echo ""

# Search for example directories (those with CMakePresets.json)
EXAMPLE_DIRS=$(find "$SCRIPT_DIR" -name "CMakePresets.json" -not -path "*/build*" | xargs -I{} dirname {} | sort)

if [ -z "$EXAMPLE_DIRS" ]; then
    echo -e "${RED}No example directories found!${NC}"
    exit 1
fi

# Count examples
TOTAL=$(echo "$EXAMPLE_DIRS" | wc -l | tr -d ' ')
echo -e "Found $TOTAL examples"
echo ""

# Statistics variables
CURRENT=0
SUCCESS=0
FAILED=0
FAILED_LIST=()

# Clean TrussC shared build if --clean
if [ "$CLEAN_BUILD" = true ]; then
    TRUSSC_DIR="$(dirname "$SCRIPT_DIR")/trussc"
    echo -e "${YELLOW}Cleaning TrussC shared build...${NC}"
    rm -rf "$TRUSSC_DIR/build-$PRESET"
fi

# Execute build for each example
for EXAMPLE_DIR in $EXAMPLE_DIRS; do
    # Extract example name
    EXAMPLE_NAME=$(echo "$EXAMPLE_DIR" | sed "s|$SCRIPT_DIR/||")
    CURRENT=$((CURRENT + 1))

    echo -e "${YELLOW}[$CURRENT/$TOTAL] Building: $EXAMPLE_NAME${NC}"

    cd "$EXAMPLE_DIR"

    # If clean build, delete build directory
    if [ "$CLEAN_BUILD" = true ]; then
        rm -rf "build-$PRESET"
    fi

    # CMake configure with preset
    if [ "$VERBOSE" = true ]; then
        if ! cmake --preset "$PRESET"; then
            echo -e "${RED}  CMake configure failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (cmake)")
            continue
        fi
    else
        if ! cmake --preset "$PRESET" > /dev/null 2>&1; then
            echo -e "${RED}  CMake configure failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (cmake)")
            continue
        fi
    fi

    # Build with preset
    if [ "$VERBOSE" = true ]; then
        if cmake --build --preset "$PRESET"; then
            echo -e "${GREEN}  Success!${NC}"
            SUCCESS=$((SUCCESS + 1))
        else
            echo -e "${RED}  Build failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (build)")
        fi
    else
        if cmake --build --preset "$PRESET" > /dev/null 2>&1; then
            echo -e "${GREEN}  Success!${NC}"
            SUCCESS=$((SUCCESS + 1))
        else
            echo -e "${RED}  Build failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (build)")
        fi
    fi
done

# Result summary
echo ""
echo -e "${BLUE}=== Build Summary ===${NC}"
echo -e "Total:   $TOTAL"
echo -e "Success: ${GREEN}$SUCCESS${NC}"
echo -e "Failed:  ${RED}$FAILED${NC}"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo -e "${RED}Failed examples:${NC}"
    for FAILED_EXAMPLE in "${FAILED_LIST[@]}"; do
        echo -e "  - $FAILED_EXAMPLE"
    done
    exit 1
fi

echo ""
echo -e "${GREEN}All examples built successfully!${NC}"
exit 0
