#!/usr/bin/env bash
# Regenerate the API outputs on macOS (the committed reference platform).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/docs/reference/build-regenerate"
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != --shaders-only ) ]]; then
    echo "Usage: $0 [--shaders-only]" >&2
    exit 2
fi

# Use the core's pinned compiler and shader recipe, without compiling the core.
python3 "$ROOT/tools/check_build_warnings.py" --run-build -- cmake -S "$ROOT/core" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
python3 "$ROOT/tools/check_build_warnings.py" --run-build -- cmake --build "$BUILD" --target TrussC_shaders_gen --config Release
[[ "${1:-}" != --shaders-only ]] || exit 0

cd "$ROOT/docs/reference"
# Keep scratch files and website outputs inside this checkout; no sibling
# trussc.org checkout is required. A fresh structure is shared by all emitters.
mkdir -p "$BUILD/web"
node --max-old-space-size=8192 structure.js --json "$BUILD/structure.json"
node generate.js --structure "$BUILD/structure.json" --out-md "$BUILD/API_INDEX.md"
node emit-forai.js
node emit-of.js --json "$BUILD/web/of-mapping.json"
node emit-coverage.js
node "$ROOT/addons/tcxLua/tools/luagen/luagen.js" reference-data.json \
    > "$ROOT/addons/tcxLua/src/generated/trussc_generated.cpp"
node "$ROOT/addons/tcxLua/tools/luagen-types/luagen-types.js" reference-data.json colors.json
node ../scripts/emit-web.js --out "$BUILD/web/trussc-api.js"
node ../scripts/emit-sketch-api.js --out "$BUILD/web/trusssketch-api.js"
node ../scripts/emit-sketch-reference.js --out "$BUILD/web/trusssketch-ref.js"
