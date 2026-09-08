#!/usr/bin/env bash
# Build and run the binding-surface check. Prints a machine-parseable report.
# Pass --regen to refresh expected.lua from the current bindings first.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ "${1:-}" == "--regen" ]] && bash "$HERE/regen_expected.sh"

cd "$HERE"
trusscli update  >/dev/null
trusscli build   >/dev/null
# macOS builds an .app bundle; every other platform a bare executable.
APP="$HERE/bin/bindcheck.app/Contents/MacOS/bindcheck"
[[ -x "$APP" ]] || APP="$HERE/bin/bindcheck"
[[ -x "$APP" ]] || { echo "bindcheck binary not found under $HERE/bin" >&2; exit 1; }
cd "$HERE/bin"
# The app runs the check in setup() then calls requestExitApp(), so it self-exits.
"$APP" 2>/dev/null | sed -n '/##BINDCHECK_BEGIN##/,/##BINDCHECK_END##/p'
