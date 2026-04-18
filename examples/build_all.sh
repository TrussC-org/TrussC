#!/bin/bash
# =============================================================================
# build_all.sh - Batch Build script for all examples
# =============================================================================
# Usage:
#   ./build_all.sh          # Update & Build all examples (Native)
#   ./build_all.sh --clean  # Clean build
#   ./build_all.sh --web    # Also build for WebAssembly
#   ./build_all.sh --help   # Show help

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
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
detect_platform() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "linux"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "msys"* ]] || [[ "$OSTYPE" == "cygwin"* ]]; then
        echo "windows"
    else
        echo "linux"  # fallback
    fi
}

PLATFORM=$(detect_platform)

# Find trusscli executable
find_pg() {
    local PG_BIN=""
    if [ "$PLATFORM" == "macos" ]; then
        PG_BIN="$ROOT_DIR/tools/bin/trusscli.app/Contents/MacOS/trusscli"
    elif [ "$PLATFORM" == "windows" ]; then
        PG_BIN="$ROOT_DIR/tools/bin/trusscli.exe"
    else
        PG_BIN="$ROOT_DIR/tools/bin/trusscli"
    fi

    if [ ! -f "$PG_BIN" ]; then
        echo -e "${RED}trusscli not found at: $PG_BIN${NC}"
        echo "Please build trusscli first (run tools/build_*.{sh,command,bat})."
        exit 1
    fi
    echo "$PG_BIN"
}

# Show help
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --clean    Clean build (delete build directories and rebuild)"
    echo "  --verbose  Show detailed build output"
    echo "  --web      Also build for WebAssembly using Emscripten"
    echo "  --help     Show this help"
    echo ""
    echo "Examples:"
    echo "  $0              # Build all examples for current platform"
    echo "  $0 --clean      # Clean build"
    echo "  $0 --web        # Build Native + Web"
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

PG_BIN=$(find_pg)

echo -e "${BLUE}=== TrussC Examples Batch Build ===${NC}"
echo -e "${YELLOW}Platform: $PLATFORM${NC}"
echo -e "${YELLOW}ProjectGenerator: $PG_BIN${NC}"
echo ""

# 1. Search for core examples in examples/
# Exclude templates, tools, build dirs, AND bin dirs, emscripten, CMakeFiles
CORE_EXAMPLES=$(find "$SCRIPT_DIR" -type d -name "src" \
    -not -path "*/templates/*" \
    -not -path "*/tools/*" \
    -not -path "*/build*" \
    -not -path "*/bin/*" \
    -not -path "*/emscripten/*" \
    -not -path "*/CMakeFiles/*" \
    | xargs -I{} dirname {})

# 2. Search for addon examples in addons/
# Look for directories starting with "example-" inside addons/* (depth 2)
ADDONS_DIR="$ROOT_DIR/addons"
ADDON_EXAMPLES=""
if [ -d "$ADDONS_DIR" ]; then
    ADDON_EXAMPLES=$(find "$ADDONS_DIR" -mindepth 2 -maxdepth 2 -type d -name "example-*" \
        -not -path "*/build*" \
        -not -path "*/bin/*" \
        -not -path "*/emscripten/*" \
        -not -path "*/CMakeFiles/*")
fi

# Combine and sort
EXAMPLE_DIRS=$(echo -e "$CORE_EXAMPLES\n$ADDON_EXAMPLES" | sort | uniq | grep -v "^$")

if [ -z "$EXAMPLE_DIRS" ]; then
    echo -e "${RED}No example directories found!${NC}"
    exit 1
fi

# Count examples
TOTAL=$(echo "$EXAMPLE_DIRS" | wc -l | tr -d ' ')
echo -e "Found $TOTAL examples"
echo ""

# ---------------------------------------------------------
# 1. Update all projects
# ---------------------------------------------------------
CURRENT=0
echo -e "${YELLOW}Updating projects...${NC}"

for EXAMPLE_DIR in $EXAMPLE_DIRS; do
    EXAMPLE_NAME=${EXAMPLE_DIR#$ROOT_DIR/}
    CURRENT=$((CURRENT + 1))
    
    if [ "$VERBOSE" = true ]; then
        echo "[$CURRENT/$TOTAL] Updating: $EXAMPLE_NAME"
    fi

    PG_ARGS="--update \"$EXAMPLE_DIR\" --tc-root \"$ROOT_DIR\""
    if [ "$WEB_BUILD" = true ]; then
        PG_ARGS="$PG_ARGS --web"
    fi
    
    if [ "$VERBOSE" = true ]; then
        eval "$PG_BIN $PG_ARGS"
    else
        OUTPUT=$(eval "$PG_BIN $PG_ARGS" 2>&1)
        RET=$?
        if [ $RET -ne 0 ]; then
            echo -e "${RED}Update failed for $EXAMPLE_NAME${NC}"
            echo "$OUTPUT"
            # Don't exit, try to continue? or fail fast?
            # Fail fast is better for consistency
            exit 1
        fi
    fi
done

# ---------------------------------------------------------
# 2. Generate Root CMakeLists.txt
# ---------------------------------------------------------
echo ""
echo -e "${YELLOW}Generating root CMakeLists.txt...${NC}"
ROOT_CMAKE="$SCRIPT_DIR/CMakeLists.txt"

cat > "$ROOT_CMAKE" <<EOF
cmake_minimum_required(VERSION 3.20)
project(TrussC_All_Examples)

# Set TRUSSC_DIR relative to examples/
set(TRUSSC_DIR "\${CMAKE_CURRENT_SOURCE_DIR}/../core")

# Add TrussC first (shared build)
if(EMSCRIPTEN)
    set(_TC_BUILD_DIR "\${TRUSSC_DIR}/build-web")
elseif(APPLE)
    set(_TC_BUILD_DIR "\${TRUSSC_DIR}/build-macos")
elseif(WIN32)
    set(_TC_BUILD_DIR "\${TRUSSC_DIR}/build-windows")
else()
    set(_TC_BUILD_DIR "\${TRUSSC_DIR}/build-linux")
endif()

if(NOT TARGET TrussC)
    add_subdirectory(\${TRUSSC_DIR} \${_TC_BUILD_DIR})
endif()

EOF

# Add subdirectories
for EXAMPLE_DIR in $EXAMPLE_DIRS; do
    # Get relative path using python3 (cross-platform way)
    REL_PATH=$(python3 -c "import os.path; print(os.path.relpath('$EXAMPLE_DIR', '$SCRIPT_DIR'))")
    # Convert backslashes to slashes for Windows
    REL_PATH=${REL_PATH//\\//}
    
    # Check if path is outside current source dir (starts with ../)
    if [[ "$REL_PATH" == ../* ]]; then
        # Need explicit binary dir
        # Generate a unique binary dir name from path (e.g. ../addons/tcxBox2d/example-basic -> addons_tcxBox2d_example-basic)
        BIN_NAME=$(echo "$REL_PATH" | sed 's/\.\.\///g' | sed 's/\//_/g')
        echo "add_subdirectory(\"$REL_PATH\" \"$BIN_NAME\")" >> "$ROOT_CMAKE"
    else
        echo "add_subdirectory(\"$REL_PATH\")" >> "$ROOT_CMAKE"
    fi
done

echo "Added $TOTAL examples to CMakeLists.txt"

# ---------------------------------------------------------
# 3. Build Native
# ---------------------------------------------------------
echo ""
echo -e "${YELLOW}Starting Native Batch Build...${NC}"
BUILD_DIR="build-$PLATFORM"

if [ "$CLEAN_BUILD" = true ]; then
    rm -rf "$BUILD_DIR"
fi

# Configure
echo "Configuring..."
if [ "$VERBOSE" = true ]; then
    if ! cmake -S . -B "$BUILD_DIR"; then
        echo -e "${RED}  Native Configure Failed${NC}"
        FAILED=$((FAILED + 1))
        FAILED_LIST+=("$EXAMPLE_NAME (native-conf)")
        continue
    fi
else
    if ! cmake -S . -B "$BUILD_DIR" > /dev/null 2>&1; then
        echo -e "${RED}  Native Configure Failed${NC}"
        # Show output on failure
        cmake -S . -B "$BUILD_DIR"
        FAILED=$((FAILED + 1))
        FAILED_LIST+=("$EXAMPLE_NAME (native-conf)")
        continue
    fi
fi

    # Build Native
    echo "Building... (Parallel)"
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    if [ "$VERBOSE" = true ]; then
        if ! cmake --build "$BUILD_DIR" -j "$JOBS"; then
            echo -e "${RED}  Native Build Failed${NC}"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (native-build)")
            continue
        fi
    else
        if ! cmake --build "$BUILD_DIR" -j "$JOBS" > /dev/null 2>&1; then
            echo -e "${RED}  Native Build Failed${NC}"
            # Show output on failure
            cmake --build "$BUILD_DIR" -j "$JOBS"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$EXAMPLE_NAME (native-build)")
            continue
        fi
    fi
    
    echo -e "${GREEN}Native Batch Build Success!${NC}"

    # ---------------------------------------------------------
    # 4. Build Web (if requested)
    # ---------------------------------------------------------
    if [ "$WEB_BUILD" = true ]; then
        echo ""
        echo -e "${YELLOW}Starting Web Batch Build...${NC}"
        BUILD_DIR_WEB="build-web"
        
        if [ "$CLEAN_BUILD" = true ]; then
            rm -rf "$BUILD_DIR_WEB"
        fi
        
        if ! command -v emcmake &> /dev/null; then
            echo -e "${RED}emcmake not found. Please setup Emscripten environment (source emsdk_env.sh).${NC}"
            exit 1
        fi

        # Configure
        echo "Configuring Web..."
        if [ "$VERBOSE" = true ]; then
            if ! emcmake cmake -S . -B "$BUILD_DIR_WEB"; then
                echo -e "${RED}  Web Configure Failed${NC}"
                FAILED=$((FAILED + 1))
                FAILED_LIST+=("$EXAMPLE_NAME (web-conf)")
                continue
            fi
        else
            if ! emcmake cmake -S . -B "$BUILD_DIR_WEB" > /dev/null 2>&1; then
                echo -e "${RED}  Web Configure Failed${NC}"
                emcmake cmake -S . -B "$BUILD_DIR_WEB"
                FAILED=$((FAILED + 1))
                FAILED_LIST+=("$EXAMPLE_NAME (web-conf)")
                continue
            fi
        fi
        
        # Build
        echo "Building Web... (Parallel)"
        if [ "$VERBOSE" = true ]; then
            if ! cmake --build "$BUILD_DIR_WEB" -j "$JOBS"; then
                echo -e "${RED}  Web Build Failed${NC}"
                FAILED=$((FAILED + 1))
                FAILED_LIST+=("$EXAMPLE_NAME (web-build)")
                continue
            fi
        else
            if ! cmake --build "$BUILD_DIR_WEB" -j "$JOBS" > /dev/null 2>&1; then
                echo -e "${RED}  Web Build Failed${NC}"
                cmake --build "$BUILD_DIR_WEB" -j "$JOBS"
                FAILED=$((FAILED + 1))
                FAILED_LIST+=("$EXAMPLE_NAME (web-build)")
                continue
            fi
        fi
        
        echo -e "${GREEN}Web Batch Build Success!${NC}"
    fi
# Cleanup root CMakeLists.txt (optional, maybe keep it for debugging)
# rm "$ROOT_CMAKE"

echo ""
echo -e "${GREEN}All Done!${NC}"
exit 0
