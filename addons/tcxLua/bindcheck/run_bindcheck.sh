#!/bin/zsh
# Build and run the binding-surface check. Prints a machine-parseable report.
# Pass --regen to refresh expected.lua from the current bindings first.
set -eu
HERE=${0:A:h}
[[ "${1:-}" == "--regen" ]] && zsh "$HERE/regen_expected.sh"

cd "$HERE"
trusscli update  >/dev/null
trusscli build   >/dev/null
APP="$HERE/bin/bindcheck.app/Contents/MacOS/bindcheck"
cd "$HERE/bin"
# The app runs the check in setup() then calls requestExitApp(), so it self-exits.
"$APP" 2>/dev/null | sed -n '/##BINDCHECK_BEGIN##/,/##BINDCHECK_END##/p'
