#!/usr/bin/env bash
# Regenerate bin/data/expected.lua from the current bindings.
# Run this AFTER editing the Lua bindings (trussc_generated.cpp / tcxLua.cpp)
# so the existence check covers the new surface.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON="$(dirname "$HERE")"            # .../addons/tcxLua
G="$ADDON/src/generated/trussc_generated.cpp"
H="$ADDON/src/tcxLua.cpp"
# luagen-types Phase 2 usertypes — may be a single file or N shard TUs
GT=("$ADDON"/src/generated/trussctype_generated*.cpp)
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
  {
    # literal registrations: new_usertype<T>("Name", ...)
    { grep -vE '^[[:space:]]*//' "$H"; grep -vhE '^[[:space:]]*//' "${GT[@]}"; } \
      | grep -oE 'new_usertype<.*>\([[:space:]]*"[^"]+"' \
      | sed -E 's/.*\(\s*"([^"]+)"/\1/'
    # helper registrations: defineTween<Tween<float>, float>(lua, "TweenFloat").
    # The helper calls new_usertype<T>(name) with a RUNTIME name, so the literal
    # grep above can never see it -- and that gap is exactly why bindcheck stayed
    # green while the generated Tween_float shadowed TweenFloat's methods. Pick
    # the name up at the call site instead.
    grep -vE '^[[:space:]]*//' "$H" \
      | grep -oE 'define[A-Z][A-Za-z]*<.*>\([^;]*"[^"]+"' \
      | sed -E 's/.*"([^"]+)"/\1/'
  } | sort -u | awk '{printf "    \"%s\",\n", $0}'
  echo "  },"
  echo "}"
} > "$OUT"

echo "wrote $OUT"
echo "functions: $(grep -c '"' "$OUT")"  # rough line count
