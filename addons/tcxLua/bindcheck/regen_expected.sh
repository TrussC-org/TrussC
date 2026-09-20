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
TMP_M="$(mktemp)"; TMP_H="$(mktemp)"
trap 'rm -f "$TMP_M" "$TMP_H"' EXIT

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

  # Per-usertype MEMBER names. The two lists above only prove a NAME exists,
  # which is why the Tween double registration stayed green: "TweenFloat" was
  # reachable while every one of its methods had been lost to a colliding
  # metatable. Three registration shapes have to be read to recover the members:
  #   generated shards : new_usertype<T>("Name")   then  t["member"] = ...
  #   tcxLua.cpp       : new_usertype<T>("Name",   then  "member", &T::member
  #   helper template  : new_usertype<T>(name,     then  "member", &T::member
  # The third names its usertype at the CALL SITE, so its members are collected
  # under @HELPER and fanned out over every defineXxx<First, ...>(lua, "Name").
  # Each row carries the C++ owner too, because classify_members.js needs it to
  # look the member up in reference-data and decide whether Lua can probe it.
  {
    awk '
      /^[[:space:]]*\/\// { next }
      match($0, /new_usertype<.*>\([[:space:]]*"[^"]+"/) {
          cpp = $0; sub(/.*new_usertype</, "", cpp); sub(/>\([[:space:]]*".*/, "", cpp)
          gsub(/trussc::/, "", cpp)
          lua = $0; sub(/.*new_usertype<.*>\([[:space:]]*"/, "", lua); sub(/".*/, "", lua)
          cur = lua; owner = cpp; inut = 1; next
      }
      match($0, /new_usertype<[^>]*>\([[:space:]]*name/) { cur = "@HELPER"; owner = "@HELPER"; inut = 1; next }
      inut && match($0, /^[[:space:]]*"[A-Za-z_][A-Za-z0-9_]*",/) {
          m = $0; sub(/^[[:space:]]*"/, "", m); sub(/".*/, "", m)
          print cur "\t" owner "\t" m; next
      }
      inut && match($0, /t\["[A-Za-z_][A-Za-z0-9_]*"\]/) {
          m = substr($0, RSTART, RLENGTH); gsub(/t\["|"\]/, "", m)
          print cur "\t" owner "\t" m; next
      }
      /^[[:space:]]*\);[[:space:]]*$/ { inut = 0 }
    ' "${GT[@]}" "$H" "$ADDON/src/tcxLua.h" | sort -u > "$TMP_M"

    # helper call sites: "LuaName<TAB>CppOwner", the owner being the first
    # template argument (defineTween<Tween<float>, float> -> Tween).
    grep -vE '^[[:space:]]*//' "$H" \
      | grep -oE 'define[A-Z][A-Za-z]*<[^;]*"[^"]+"' \
      | while IFS= read -r call; do
            first="${call#*<}"; first="${first%%,*}"; first="${first%%>*}"; first="${first%%<*}"
            name="$(printf '%s' "$call" | sed -E 's/.*"([^"]+)".*/\1/')"
            printf '%s\t%s\n' "$name" "$first"
        done | sort -u > "$TMP_H"

    awk -F'\t' -v helpers="$TMP_H" '
      BEGIN { n = 0; while ((getline h < helpers) > 0) { split(h, p, "\t"); hl[++n] = p[1]; ho[n] = p[2] } }
      $1 == "@HELPER" { for (i = 1; i <= n; i++) print hl[i] "\t" ho[i] "\t" $3; next }
      { print }
    ' "$TMP_M" | sort -u | node "$HERE/classify_members.js" "$ADDON/../../docs/reference/reference-data.json"
  }
  echo "}"
} > "$OUT"

echo "wrote $OUT"
echo "functions: $(grep -c '"' "$OUT")"  # rough line count
