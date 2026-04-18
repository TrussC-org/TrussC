#!/bin/bash

# Generate documentation for AI consumption
# Output:
#   - trussc_docs.md     : Documentation (markdown files)
#   - trussc_examples.md : Example source code
#   - trussc_api.md      : API headers

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Timestamp for filenames (e.g., 2025-1226-2310)
TIMESTAMP=$(date +"%Y-%m%d-%H%M")
# Date for file content (English format)
DATE_STR=$(LANG=C date +"%Y-%m-%d %H:%M:%S")

# Output files (with timestamp prefix)
DOCS_FILE="$SCRIPT_DIR/${TIMESTAMP}_trussc_docs.md"
EXAMPLES_FILE="$SCRIPT_DIR/${TIMESTAMP}_trussc_examples.md"
API_FILE="$SCRIPT_DIR/${TIMESTAMP}_trussc_api.md"

# Function to add a file with path and code block
add_file() {
    local file="$1"
    local output="$2"
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

    echo "" >> "$output"
    echo "## $rel_path" >> "$output"
    echo "" >> "$output"
    echo "\`\`\`$lang" >> "$output"
    cat "$file" >> "$output"
    echo "" >> "$output"
    echo "\`\`\`" >> "$output"
}

echo "Generating TrussC documentation for AI..."

# =============================================================================
# 1. Documentation (trussc_docs.md)
# =============================================================================
echo "# TrussC Documentation" > "$DOCS_FILE"
echo "" >> "$DOCS_FILE"
echo "Generated: $DATE_STR" >> "$DOCS_FILE"

echo "  [1/3] Collecting documentation..."

find "$ROOT_DIR" -name "*.md" -type f \
    ! -path "*/build/*" \
    ! -path "*/.git/*" \
    ! -path "*/libs/*" \
    ! -path "*/docs/scripts/*" \
    ! -name "*_jp.md" | sort | while read f; do
    add_file "$f" "$DOCS_FILE"
done

# =============================================================================
# 2. Examples (trussc_examples.md)
# =============================================================================
echo "# TrussC Examples" > "$EXAMPLES_FILE"
echo "" >> "$EXAMPLES_FILE"
echo "Generated: $DATE_STR" >> "$EXAMPLES_FILE"

echo "  [2/3] Collecting examples..."

find "$ROOT_DIR/examples" \( -name "*.h" -o -name "*.cpp" \) -type f \
    ! -path "*/build/*" | sort | while read f; do
    add_file "$f" "$EXAMPLES_FILE"
done

# =============================================================================
# 3. API Headers (trussc_api.md)
# =============================================================================
echo "# TrussC API" > "$API_FILE"
echo "" >> "$API_FILE"
echo "Generated: $DATE_STR" >> "$API_FILE"

echo "  [3/3] Collecting API headers..."

# TrussC's own headers in tc/ directory (headers only, no .cpp)
find "$ROOT_DIR/core/include/tc" -name "*.h" -type f | sort | while read f; do
    add_file "$f" "$API_FILE"
done

# TrussC's own headers in root include (tc*.h and TrussC.h only)
find "$ROOT_DIR/core/include" -maxdepth 1 -name "tc*.h" -type f | sort | while read f; do
    add_file "$f" "$API_FILE"
done
add_file "$ROOT_DIR/core/include/TrussC.h" "$API_FILE"

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "Generated files:"
for f in "$DOCS_FILE" "$EXAMPLES_FILE" "$API_FILE"; do
    SIZE=$(du -h "$f" | cut -f1)
    LINES=$(wc -l < "$f")
    echo "  $(basename "$f"): $SIZE ($LINES lines)"
done
