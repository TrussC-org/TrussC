#!/bin/bash

# Generate comprehensive documentation for AI consumption
# Output: docs/scripts/docsForAI.md

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUTPUT_FILE="$SCRIPT_DIR/docsForAI.md"

echo "# TrussC Complete Reference" > "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "Generated: $(date)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Function to add a file with path and code block
add_file() {
    local file="$1"
    local rel_path="${file#$ROOT_DIR/}"
    local ext="${file##*.}"

    # Determine language for code block
    local lang=""
    case "$ext" in
        md) lang="markdown" ;;
        h|hpp) lang="cpp" ;;
        cpp|cc) lang="cpp" ;;
        mm) lang="objcpp" ;;
        cmake) lang="cmake" ;;
        *) lang="" ;;
    esac

    echo "" >> "$OUTPUT_FILE"
    echo "## $rel_path" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
    echo "\`\`\`$lang" >> "$OUTPUT_FILE"
    cat "$file" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
    echo "\`\`\`" >> "$OUTPUT_FILE"
}

echo "Collecting files..."

# 1. All markdown files (excluding third-party libs)
echo "--- Markdown Documentation ---" >> "$OUTPUT_FILE"
find "$ROOT_DIR" -name "*.md" -type f \
    ! -path "*/build/*" \
    ! -path "*/.git/*" \
    ! -path "*/libs/*" \
    ! -name "docsForAI.md" | sort | while read f; do
    add_file "$f"
done

# 2. All example source files (.h, .cpp)
echo "" >> "$OUTPUT_FILE"
echo "--- Examples Source Code ---" >> "$OUTPUT_FILE"
find "$ROOT_DIR/examples" \( -name "*.h" -o -name "*.cpp" \) -type f ! -path "*/build/*" | sort | while read f; do
    add_file "$f"
done

# 3. TrussC header files (excluding third-party libraries)
echo "" >> "$OUTPUT_FILE"
echo "--- TrussC Headers ---" >> "$OUTPUT_FILE"

# TrussC's own headers in tc/ directory
find "$ROOT_DIR/trussc/include/tc" -name "*.h" -type f | sort | while read f; do
    add_file "$f"
done

# TrussC's own headers in root include (tc*.h and TrussC.h only)
find "$ROOT_DIR/trussc/include" -maxdepth 1 -name "tc*.h" -type f | sort | while read f; do
    add_file "$f"
done
add_file "$ROOT_DIR/trussc/include/TrussC.h"

# Note: Excluding third-party libraries (sokol/, imgui/, stb/, miniaudio.h, dr_*.h, pugixml/, etc.)

# 4. TrussC implementation files (.cpp, .mm)
echo "" >> "$OUTPUT_FILE"
echo "--- TrussC Implementation ---" >> "$OUTPUT_FILE"

# tc/*.cpp (network, sound, etc.)
find "$ROOT_DIR/trussc/include/tc" -name "*.cpp" -type f | sort | while read f; do
    add_file "$f"
done

# platform/*.cpp and *.mm
find "$ROOT_DIR/trussc/platform" \( -name "*.cpp" -o -name "*.mm" \) -type f | sort | while read f; do
    add_file "$f"
done

# Calculate size
SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
LINES=$(wc -l < "$OUTPUT_FILE")

echo ""
echo "Generated: $OUTPUT_FILE"
echo "Size: $SIZE"
echo "Lines: $LINES"
