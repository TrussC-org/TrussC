#!/bin/zsh
# Regenerate bin/data/expected.lua from the current bindings.
# Run this AFTER editing the Lua bindings (trussc_generated.cpp / tcxLua.cpp)
# so the existence check covers the new surface.
set -eu
HERE=${0:A:h}
ADDON=${HERE:h}            # .../addons/tcxLua
G="$ADDON/src/generated/trussc_generated.cpp"
H="$ADDON/src/tcxLua.cpp"
GT="$ADDON/src/generated/trussctype_generated.cpp"   # luagen-types Phase 2 usertypes
OUT="$HERE/bin/data/expected.lua"

{
  echo "-- AUTO-GENERATED expected binding names (regen_expected.sh)"
  echo "return {"
  echo "  functions = {"
  # set_function("name", ...) from both the generated file and the hand-written one.
  # Skip commented-out lines so disabled bindings aren't counted as expected.
  { grep -vE '^[[:space:]]*//' "$G"; grep -vE '^[[:space:]]*//' "$H"; } \
    | grep -oE 'set_function\("[^"]+"' \
    | sed -E 's/set_function\("(.*)"/\1/' | sort -u \
    | awk '{printf "    \"%s\",\n", $0}'
  echo "  },"
  echo "  usertypes = {"
  { grep -vE '^[[:space:]]*//' "$H"; grep -vE '^[[:space:]]*//' "$GT"; } \
    | grep -oE 'new_usertype<.*>\([[:space:]]*"[^"]+"' \
    | sed -E 's/.*\(\s*"([^"]+)"/\1/' | sort -u \
    | awk '{printf "    \"%s\",\n", $0}'
  echo "  },"
  echo "}"
} > "$OUT"

echo "wrote $OUT"
echo "functions: $(grep -c '"' "$OUT")"  # rough line count
