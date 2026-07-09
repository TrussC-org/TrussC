#!/bin/zsh
# Build every tcxLua example and run each for ~5s to catch build breaks,
# runtime crashes (uncaught sol::error -> SIGABRT), and Lua errors.
# RUNTIME=ALIVE means the app was still looping healthily when we killed it.
set -u
HERE=${0:A:h}
ADDON=${HERE:h}            # .../addons/tcxLua
LOG="$HERE/sweep_logs"
mkdir -p "$LOG"
EXAMPLES=(exampleBasic exampleEasyCam exampleFileReload exampleJsonXml exampleLiveUpdate \
          exampleLuaJITCheck exampleMath examplePath exampleSimple exampleTween exampleTypes)

printf "%-22s %-8s %-10s %s\n" EXAMPLE BUILD RUNTIME NOTES
for d in $EXAMPLES; do
  cd "$ADDON/$d" || { printf "%-22s %s\n" "$d" "NODIR"; continue; }
  trusscli update >"$LOG/$d.update.log" 2>&1
  if ! trusscli build >"$LOG/$d.build.log" 2>&1; then
    printf "%-22s %-8s %-10s %s\n" "$d" "FAIL" "-" "$(grep -iE 'error:' "$LOG/$d.build.log" | head -1)"
    continue
  fi
  BIN=$(ls -d "$ADDON/$d/bin/"*.app/Contents/MacOS/* 2>/dev/null | head -1)
  [[ -z "$BIN" ]] && { printf "%-22s %-8s %-10s\n" "$d" "OK" "NOBIN"; continue; }
  cd "$ADDON/$d/bin"
  "$BIN" >"$LOG/$d.out.log" 2>"$LOG/$d.err.log" &
  PID=$!
  for i in $(seq 1 10); do kill -0 $PID 2>/dev/null || break; sleep 0.5; done
  if kill -0 $PID 2>/dev/null; then kill $PID 2>/dev/null; wait $PID 2>/dev/null; rt="ALIVE"
  else wait $PID 2>/dev/null; code=$?; [[ $code -eq 0 ]] && rt="EXIT0" || rt="CRASH($code)"; fi
  luaerr=$(grep -ciE 'lua.*(fail|error)|runtime error|attempt to|stack traceback' \
           "$LOG/$d.err.log" "$LOG/$d.out.log" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
  note=""; [[ "$luaerr" -gt 0 ]] && note="lua_err_lines=$luaerr (see $LOG/$d.err.log)"
  printf "%-22s %-8s %-10s %s\n" "$d" "OK" "$rt" "$note"
done
