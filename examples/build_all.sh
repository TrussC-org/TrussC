#!/bin/bash
# =============================================================================
# build_all.sh - Build script for all examples
# =============================================================================
# Usage:
#   ./build_all.sh          # Build all examples
#   ./build_all.sh --clean  # Clean build (delete CMake cache)
#   ./build_all.sh --help   # Show help

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLEAN_BUILD=false
VERBOSE=false

# Colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Show help
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --clean    Clean build (delete CMake cache and rebuild)"
    echo "  --verbose  Show detailed build output"
    echo "  --help     Show this help"
    echo ""
    echo "Examples:"
    echo "  $0              # Build all examples"
    echo "  $0 --clean      # Clean build"
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

echo -e "${BLUE}=== TrussC Examples Build Script ===${NC}"
echo ""

# Number of parallel jobs (CPU cores)
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
echo -e "Using ${JOBS} parallel jobs"
echo ""

# Search for build directories
BUILD_DIRS=$(find "$SCRIPT_DIR" -type d -name "build" | sort)

if [ -z "$BUILD_DIRS" ]; then
    echo -e "${RED}No build directories found!${NC}"
    exit 1
fi

# Statistics variables
TOTAL=0
SUCCESS=0
FAILED=0
FAILED_LIST=()

# Execute build in each build directory
for BUILD_DIR in $BUILD_DIRS; do
    # Extract example name (e.g., 3d/ofNodeExample)
    EXAMPLE_NAME=$(echo "$BUILD_DIR" | sed "s|$SCRIPT_DIR/||" | sed 's|/build$||')
    TOTAL=$((TOTAL + 1))

    echo -e "${YELLOW}[$TOTAL] Building: $EXAMPLE_NAME${NC}"

    cd "$BUILD_DIR"

    # If clean build, delete CMake cache
    if [ "$CLEAN_BUILD" = true ]; then
        rm -f CMakeCache.txt
        rm -rf CMakeFiles
    fi

    # CMake configure
    if [ "$VERBOSE" = true ]; then
        if ! cmake ..; then
            echo -e "${RED}  CMake configure failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (cmake)")
            continue
        fi
    else
        if ! cmake .. > /dev/null 2>&1; then
            echo -e "${RED}  CMake configure failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (cmake)")
            continue
        fi
    fi

    # Build (parallel build)
    if [ "$VERBOSE" = true ]; then
        if cmake --build . -j "$JOBS"; then
            echo -e "${GREEN}  Success!${NC}"
            SUCCESS=$((SUCCESS + 1))
        else
            echo -e "${RED}  Build failed!${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (build)")
        fi
    else
        if cmake --build . -j "$JOBS" > /dev/null 2>&1; then
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
