# #1256 probe: Air run plan

Probe branch only (never merged). All probe output is opt-in via `TRUSSC_PROBE_1256=1`, tagged `[probe #1256]`, on stderr. Run from the repository root of this branch on the Air (M1, internal Retina 2x), logged-in desktop session. No downloads: if the SDK or the pinned core/tools/sokol-shdc is missing, stop and report. Send back .verification/air/results.tsv and the three *-summary.txt files (keep all logs).

### Build once, offline

```bash
# Run this block with bash from the repository root.
set -eu
mkdir -p .verification/air
sw_vers > .verification/air/os.txt
uname -m >> .verification/air/os.txt
git rev-parse HEAD > .verification/air/revision.txt
xcrun --sdk macosx --show-sdk-path > .verification/air/sdk.txt
system_profiler SPDisplaysDataType > .verification/air/displays.txt
test -x core/tools/sokol-shdc/sokol-shdc

# Only if ignored generated wrappers are absent; no trusscli update/generate.
python3 - <<'PY'
from pathlib import Path
for directory, name in [('core/tests/allCoreTests', 'allCoreTests'),
                        ('addons/tcxImGui/tests-renderTarget', 'tests-renderTarget')]:
    path = Path(directory) / 'CMakeLists.txt'
    if not path.exists():
        path.write_text('cmake_minimum_required(VERSION 3.20)\n'
                        'project(' + name + ')\n'
                        'if(NOT DEFINED TRUSSC_DIR)\n'
                        '  set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core")\n'
                        'endif()\n'
                        'include(${TRUSSC_DIR}/cmake/trussc_app.cmake)\n'
                        'trussc_app()\n')
PY
cmake -S core/tests/nativeWindow -B core/tests/nativeWindow/build-macos \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DCMAKE_BUILD_TYPE=Release \
  > .verification/air/native-configure.log 2>&1
cmake --build core/tests/nativeWindow/build-macos --parallel 2 \
  > .verification/air/native-build.log 2>&1
cmake -S core/tests/allCoreTests -B core/tests/allCoreTests/build-macos \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DTC_CORE_TEST_SELECT=secondaryEventDelivery \
  -DTC_HOT_RELOAD=OFF -DCMAKE_BUILD_TYPE=Release \
  > .verification/air/core-configure.log 2>&1
cmake --build core/tests/allCoreTests/build-macos --parallel 2 \
  > .verification/air/core-build.log 2>&1
cmake -S addons/tcxImGui/tests-renderTarget \
  -B addons/tcxImGui/tests-renderTarget/build-macos \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DTC_HOT_RELOAD=OFF \
  -DCMAKE_BUILD_TYPE=Release > .verification/air/imgui-configure.log 2>&1
cmake --build addons/tcxImGui/tests-renderTarget/build-macos --parallel 2 \
  > .verification/air/imgui-build.log 2>&1
```

### Run and preserve failures (bash)

```bash
# Do not use set -e here: these tests currently fail and every log is needed.
set +e
NATIVE=core/tests/nativeWindow/build-macos/nativeWindow
CORE=core/tests/allCoreTests/bin/allCoreTests.app/Contents/MacOS/allCoreTests
IMGUI=addons/tcxImGui/tests-renderTarget/bin/tests-renderTarget.app/Contents/MacOS/tests-renderTarget
: > .verification/air/results.tsv
run_case() {
    label="$1"; shift
    "$@" > ".verification/air/$label.log" 2>&1
    code=$?
    printf '%s\t%s\n' "$label" "$code" >> .verification/air/results.tsv
}
for i in 1 2 3; do
    run_case "native-off-$i" env -u TRUSSC_PROBE_1256 "$NATIVE" --display
    run_case "native-on-$i" env TRUSSC_PROBE_1256=1 "$NATIVE" --display
 done
run_case native-no-high-dpi env TRUSSC_PROBE_1256=1 "$NATIVE" --display --no-high-dpi
# Fifty fresh processes for each mode: do not repeat inside one Runtime.
for i in $(seq 1 50); do
    run_case "secondary-off-$i" env -u TRUSSC_PROBE_1256 "$CORE" secondaryEventDelivery --display
    run_case "secondary-on-$i" env TRUSSC_PROBE_1256=1 "$CORE" secondaryEventDelivery --display
 done
for mode in --self-check --msaa4 --screen-only --secondary-exit --native-dpi2; do
    run_case "imgui-${mode#--}-off" env -u TRUSSC_PROBE_1256 "$IMGUI" "$mode"
    run_case "imgui-${mode#--}-on" env TRUSSC_PROBE_1256=1 "$IMGUI" "$mode"
 done
grep -nE '\[probe #1256\]|throwing tick_cb|ticks continue|contained|: FAIL|SKIP' \
  .verification/air/native-*.log > .verification/air/native-summary.txt
grep -nE '\[probe #1256\]|got .* expected|: FAIL|SKIP' \
  .verification/air/secondary-*.log > .verification/air/secondary-summary.txt
grep -nE '\[probe #1256\]|FAIL|SKIP' \
  .verification/air/imgui-*.log > .verification/air/imgui-summary.txt
cat .verification/air/results.tsv
```

Retain complete logs, especially successful flaky runs and both window ids.
A SKIP is not a pass of the skipped checks. Fifty independent trials at a true
30% rate would be extremely unlikely to all pass, but logging can perturb that
rate: if only unlogged runs fail, report an observer effect and keep their full
output; do not declare the bug fixed. Use the outcome tables above to choose
any follow-up Decision. Do not push a fix based only on these hypotheses.

