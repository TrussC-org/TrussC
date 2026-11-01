# #239 combined device pass

The probe branch is next/window df87610f plus this test-only probe. All other steps test df87610f code. Each launch appends to the same durable probe log; send that file along with the per-step stdout logs. Every mode draws its instructions on screen. No arguments (or an unknown mode) prints usage and exits 0 without a window. Do not install missing prerequisites without owner approval.

Build order matters: core sweep, then addon sweep, then the standalone nativeWindow harness (build_all does not rebuild it; always configure and build it fresh after checkout), then the hot-reload host rebuild. Run the commands in the order shown.

The probe observes public events only. `rawEvent.window_id` is the same positive creation ID as `tc_list_windows`; before its first raw event the screen says `pending-raw-event`, never a guessed ID. `ExitRequestEventArgs.reason` is a veto explanation and may be empty on this base; the framework final exit line carries causes such as `session-end`, `last-window-close`, or `app-quit`. A session-end request stands for macOS `applicationShouldTerminate` / Windows `WM_QUERYENDSESSION`; exit/cleanup and the framework final `exit: clean` stand for accepted termination / `WM_ENDSESSION`. Native query/cancel/power-off callbacks are not directly observable. The probe adds no hooks, headless mode or artificial idle delay; timeout keys change the watchdog deadline, not cleanup duration. Those unobservable scenarios remain UNVERIFIED.

In the probe log, the success marker for a normal ending is the framework line `[System] exit: clean …` (with its cause, code and PID). The probe's own last line is `probe: callbacks balanced` (each attached App got exit and cleanup once) or `exit: incomplete …`; it is not a substitute for the framework line.

| Device | Steps | [HUMAN] steps | Rough time |
|---|---:|---:|---|
| Air | 10 | 7 | 3–5 hours plus session restarts |
| win-regression-test | 10 | 9 | 3–5 hours plus session restarts |
| trussc-gdm | 4 | 3 | 45–90 minutes |
| RasPi | 4 | 1 | 2–3 hours |

## Air — macOS Apple Silicon / Metal and iOS

Run from the repository root in a logged-in graphical desktop. Do not install tools without the owner's OK. If a prerequisite is missing, record UNVERIFIED; if a platform feature is unavailable, record SKIP. Use a local clone with no work that checkout would overwrite.

```sh
git fetch origin probe/an2-device && git checkout --detach origin/probe/an2-device
git rev-parse HEAD
mkdir -p "$HOME/device-239-air"
trusscli update -p core/tests/an2DeviceProbe --tc-root "$PWD" --ide cmake
cmake -S core/tests/an2DeviceProbe -B core/tests/an2DeviceProbe/build-macos -DTRUSSC_DIR="$PWD/core" -DCMAKE_BUILD_TYPE=Release
cmake --build core/tests/an2DeviceProbe/build-macos --config Release
PROBE=core/tests/an2DeviceProbe/bin/an2DeviceProbe.app/Contents/MacOS/an2DeviceProbe
python3 examples/build_all.py --core-tests-only --include-daily 2>&1 | tee "$HOME/device-239-air/build-core.log"
python3 examples/build_all.py --addon-tests-only --include-daily 2>&1 | tee "$HOME/device-239-air/build-addon.log"
cmake -S core/tests/nativeWindow -B core/tests/nativeWindow/build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build core/tests/nativeWindow/build-macos --config Release
# Rebuild the hot-reload host AFTER the addon sweep (the sweep relinks libTrussC; a stale host exits 65 "host older than TrussC"):
cmake --build core/tests/hotReloadLifecycle/build-macos --config Release
```

The build commands also run the tests. Check their summaries and record their actual status, including any missing prerequisite. For individual generated projects use `trusscli update -p <project> --tc-root "$PWD" --ide cmake`, then `cmake -S <project> -B <project>/build-macos -DCMAKE_BUILD_TYPE=Release` and `cmake --build <project>/build-macos --config Release`. Use the installed trusscli; installing one needs the owner's OK.

1. Run `for i in $(seq 1 17); do core/tests/nativeWindow/build-macos/nativeWindow --display > "$HOME/device-239-air/1-run$i.log" 2>&1; echo "run $i: $?"; done` (17 runs, one log each); also run `core/tests/nativeWindow/build-macos/nativeWindow --display --no-high-dpi > "$HOME/device-239-air/1-control.log" 2>&1; echo $?`. Expect exit 0 and all checks PASS, including fullscreen reversal, display link/fallback, secondary creation from callbacks, close during update/render, per-show and constructor-failure diagnostics, and native performClose checks. **[HUMAN]** Observe windows and fullscreen/Spaces transitions for hangs, incorrect restoration, or black frames; record anything observed. This does not establish the separate pwsClose main/mainasync Runtime test: no verified command for that device scenario was found; report that part UNVERIFIED.
2. Run `core/tests/allCoreTests/bin/allCoreTests.app/Contents/MacOS/allCoreTests appEventDelivery --display > "$HOME/device-239-air/2.log" 2>&1; echo $?`, then the same executable with `appEventDelivery --exit-order`, `threadSafety`, `asyncLoads`, and `consoleBuildBounds` (the last ten times). Expect exit 0 and PASS verdicts. The core build log covers the combined headlessConsole tests. Do not infer a Node/Event profile from a dladdr check.
3. Run `core/tests/sessionEndBusy/bin/sessionEndBusy.app/Contents/MacOS/sessionEndBusy --mac-device > "$HOME/device-239-air/3.log" 2>&1; echo $?`. Expect `sessionEndBusy mac idle: PASS` and `sessionEndBusy mac busy: PASS`, exit 0; each child is bounded at 15 seconds and reaped. A hung or ignoring child must FAIL. The executable location is generated by the own-binary build; if absent, mark UNVERIFIED rather than substituting a guessed path.
4. Run `core/tests/hotReloadLifecycle/bin/hotReloadLifecycle.app/Contents/MacOS/hotReloadLifecycle --reload-check > "$HOME/device-239-air/4.log" 2>&1; echo $?` and repeat with `--c4-checks`. Expect exit 0, `--reload-check: OK`, `C4 substrate: PASS`, real `TC_HOT_RELOAD(tcApp)` / `src/tcApp.cpp` warning for second and third windows, and retired module remains mapped. Nested real input/capture and graphics-pool propagation to the host are not established by these checks; report those portions UNVERIFIED unless directly observed with instrumentation.
5. Run `core/tests/allCoreTests/bin/allCoreTests.app/Contents/MacOS/allCoreTests perWindowShadows --gpu-check > "$HOME/device-239-air/5.log" 2>&1; echo $?`. Expect exit 0, three comparison lines with zero pixels outside tolerance 2; Metal secondary cases explicitly SKIP. Build and launch `examples/3d/multiShadowExample` using the per-project commands above. **[HUMAN]** See a rendered scene with cast shadows. Also run the combined `sokolPoolSizes --gpu-check` to check initial-window settings; inspect its PASS lines. Retina/nil-drawable and static-secondary release after shutdown require additional instrumentation; do not claim them from the shadow result.
6. **[HUMAN]** Build and launch `examples/windowing/imguiWindowsExample` using the per-project commands above; executable is `examples/windowing/imguiWindowsExample/bin/imguiWindowsExample.app/Contents/MacOS/imguiWindowsExample`. Click “open second window”, then move the pointer over the slider and text regions in each window. Expect each context to control the cursor only while its own window is hovered, without flicker or the other context overriding it. Record observations in `~/device-239-air/6.log`.
7. **[HUMAN]** Run `"$PROBE" drop > "$HOME/device-239-air/7-drop.log" 2>&1; echo $?`. Drop files from Finder on A and B, including 16 and >16 paths and a long path near the 2048-byte limit. Screen identifies each window; `~/device-239-probe.log` must show only the receiving ID, `filesDropped count=...` and every delivered path; verify 16-path / 2048-byte caps from actual observations. Close both windows; expect exit 0. Then run `"$PROBE" cursor > "$HOME/device-239-air/7-cursor.log" 2>&1; echo $?`. Press 1–5, B (generated 32×32 Custom0), U, H and S in each window, moving the pointer between them after every change. Both screens show the same app-wide state; log shows each change and receiver. Expect consistent appearance/visibility. Close both; expect exit 0. Send the durable log and both stdout logs.
8. **[HUMAN]** Run `"$PROBE" close > "$HOME/device-239-air/8.log" 2>&1; echo $?`. A is quitOnClose and B ordinary. V toggles the initially ON one-shot veto. First close A with its title-bar ×: both stay, Runtime logs veto=1, then auto-disarms. Repeat: exit 0, each App logs exit/cleanup count=1 and atexit logs process exit once. Relaunch for C in A (focused = last key receiver); code close must omit closeRequested. Relaunch, close B first, then repeat veto/accept on last-window A. V can rearm before each case. Screen shows veto and focused receiver. Keep each run's stdout and `~/device-239-probe.log`; a duplicate exit is a failure.
9. **[HUMAN] LAST: real session end.** Complete steps 1–8 and 10 first. Run `"$PROBE" session > "$HOME/device-239-air/9.log" 2>&1; echo $?`. Screen explains the public/native mapping. Start at 2 s; T cycles 5, 10, 2 s and logs `setLogoutTimeout`. In separate launches perform real logout/restart/shutdown, idle at each timeout, then D followed by logout during the logged 30 s main-thread block. After logging in inspect `~/device-239-probe.log`: idle should retain exit/cleanup and the framework final `exit: clean`; busy termination may skip App callbacks and atexit. Busy main is intentionally accepted behavior, not a new fix. Measure OS deadlines separately. Public exitRequested logs args.reason (possibly empty); the framework final line carries session-end. Native applicationShouldTerminate, power-off notification and query cancellation are not directly logged and must remain UNVERIFIED. `sessionEndBusy --mac-device` is a synthetic seam only. Do not infer real native callback coverage from it.
10. **[HUMAN] iOS.** Simulator is not a real-device orientation test; record real rotation UNVERIFIED if no device. Generate an iOS project with `trusscli update -p core/tests/allCoreTests --tc-root "$PWD" --ide cmake --ios`, and configure with `cmake -S core/tests/allCoreTests -B core/tests/allCoreTests/build-ios-sim -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"` followed by `cmake --build core/tests/allCoreTests/build-ios-sim --config Release`. The generated app path and bundle ID depend on the generated project: inspect the build output and Info.plist before using `xcrun simctl install booted <app>` and `xcrun simctl launch --console-pty booted <bundle-id> perWindowShadows --gpu-check`. Capture stdout to `~/device-239-air/10.log`, poll for the verdict, then run `xcrun simctl terminate booted <bundle-id>`; do not wait for launch to return. Expect three comparison lines with zero out-of-tolerance pixels. Framework NOTICE/WARNING/ERROR lines do not reach `--console-pty`; a check for them must be asserted in the harness or echoed to stdout. For launch/draw/exitApp smoke and real rotation, instrument an example and watch drawing after rotation and after exitApp; mobile exitApp does nothing. Device provisioning or missing SDK needs an install/owner OK. If the build or probe cannot be established, report UNVERIFIED.

Special exclusion: `headlessConsole --windowed-child-setup-exit` and `--windowed-child-setup-veto` produce no verdict on macOS because runApp does not return. Record both SKIP; absence of failure is not PASS. Android is not assigned to this device; Android smoke is UNVERIFIED until an Android device/session is available.

Report back one line per step: `<step#> PASS|FAIL|SKIP|UNVERIFIED <log path> <one-line note>`. An unavailable platform feature is SKIP; only a wrong result is FAIL; anything not run is UNVERIFIED, never PASS. For mixed results give separate lines with the same step number.

## win-regression-test — Windows D3D11 / MSVC and Web

Use a Visual Studio developer PowerShell in a graphical desktop, repository root. Install nothing without the owner's OK; missing dependencies are UNVERIFIED. Use a clean clone with no work checkout would overwrite. Capture GUI exit codes with `Start-Process -Wait -PassThru -NoNewWindow`, never `$LASTEXITCODE` after directly invoking a GUI executable.

```powershell
git fetch origin probe/an2-device && git checkout --detach origin/probe/an2-device
git rev-parse HEAD
New-Item -ItemType Directory -Force "$env:USERPROFILE\device-239-win" | Out-Null
trusscli update -p core/tests/an2DeviceProbe --tc-root . --ide cmake
cmake -S core/tests/an2DeviceProbe -B core/tests/an2DeviceProbe/build-windows -DTRUSSC_DIR="$PWD/core" -G "Visual Studio 17 2022" -A x64
cmake --build core/tests/an2DeviceProbe/build-windows --config Release
$probe = 'core/tests/an2DeviceProbe/bin/an2DeviceProbe.exe'
python3 examples/build_all.py --core-tests-only --include-daily 2>&1 | Tee-Object "$env:USERPROFILE\device-239-win\build-core.log"
python3 examples/build_all.py --addon-tests-only --include-daily 2>&1 | Tee-Object "$env:USERPROFILE\device-239-win\build-addon.log"
cmake -S core/tests/nativeWindow -B core/tests/nativeWindow/build-windows -G "Visual Studio 17 2022" -A x64
cmake --build core/tests/nativeWindow/build-windows --config Release
# Rebuild the hot-reload host AFTER the addon sweep (the sweep relinks libTrussC; a stale host exits 65 "host older than TrussC"):
cmake --build core/tests/hotReloadLifecycle/build-windows --config Release
C:\emsdk\emsdk_env.bat
python3 examples/build_all.py --web-only 2>&1 | Tee-Object "$env:USERPROFILE\device-239-win\build-web.log"
```

If `emsdk_env.bat` is invoked from PowerShell its environment does not persist; use `cmd /k C:\emsdk\emsdk_env.bat` and run the Web command in that cmd session. For a generated example: `trusscli update -p <project> --tc-root . --ide cmake`, `cmake -S <project> -B <project>/build-windows -G "Visual Studio 17 2022" -A x64`, `cmake --build <project>/build-windows --config Release`. The suite builds and runs tests; inspect its summaries.

1. Run `$p = Start-Process core/tests/nativeWindow/build-windows/Release/nativeWindow.exe -ArgumentList '--display' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\1.log"; $p.ExitCode`. Expect 0 and all 45 cases PASS, including native quit/close/session and throw-as-veto, N3/N4 geometry/style and independent present intervals. Repeat with `--display --session-end` and `--display --no-high-dpi`; retain separate logs. **[HUMAN]** Watch the windows for incorrect fullscreen restoration and drag a resize border while rendering; expect responsive rendering and no corruption. A real modal resize requires observation, not merely an automated verdict.
2. Run `$p = Start-Process core/tests/hotReloadLifecycle/bin/hotReloadLifecycle.exe -ArgumentList '--reload-check' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\2.log"; $p.ExitCode`; repeat with `--c4-checks`. Expect 0, `--reload-check: OK`, `C4 substrate: PASS`, second/third-window warning naming `TC_HOT_RELOAD(tcApp)` and `src/tcApp.cpp`, and retired module remains mapped. Verify no system dialog appears during the bad-guest case. **[HUMAN]** Observe the screen for a dialog. Nested real input/capture and host graphics pool settings require additional instrumentation; report those portions UNVERIFIED if not observed.
3. Run `$p = Start-Process core/tests/allCoreTests/bin/allCoreTests.exe -ArgumentList 'perWindowShadows --gpu-check' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\3.log"; $p.ExitCode`. Expect 0, 16 comparisons with zero pixels outside tolerance 2. Also run `sokolPoolSizes --gpu-check` through the same form and check PASS lines. Build and launch `examples/3d/multiShadowExample` with the project commands. **[HUMAN]** Observe cast shadows in the rendered scene.
4. **[HUMAN]** Build `examples/windowing/imguiWindowsExample` with the project commands and run `$p = Start-Process examples/windowing/imguiWindowsExample/bin/imguiWindowsExample.exe -Wait -PassThru -NoNewWindow; $p.ExitCode`. Click “open second window”, move over sliders and text in both; expect only the hovered context to change the cursor, without fighting or flicker. Write observations to `~/device-239-win/4.log`.
5. **[HUMAN]** Run `$p = Start-Process $probe -ArgumentList 'cursor' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\5.log"; $p.ExitCode`. In both windows press 1–5 (Arrow/IBeam/Crosshair/Hand/ResizeEW), B (generated Custom0), U, H and S. Move pointer across both after each change. Both screens show identical app-wide state, consistent appearance and visibility. Close both; expect 0. Also run the same Start-Process form with `-ArgumentList 'drop'` and stdout `5-drop.log`; drop files on each window and check receiving ID, count and every path in `C:\Users\Public\device-239-probe.log`. Send that durable log.
6. **[HUMAN]** Run `$p = Start-Process $probe -ArgumentList 'close' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\6.log"; $p.ExitCode`. Screen shows A quitOnClose, B ordinary, vetoNext initially ON and last key receiver. Close A via ×: first attempt leaves both windows and logs veto=1, auto-off. Repeat: exit 0, each App exit/cleanup count=1 and process exit once. Relaunch and press C in A for code close: no closeRequested expected. Relaunch, close B first, then veto/accept A as last window. V rearms veto. Log each receiver in the durable Public log; preserve separate stdout files for each run.
7. Run `$p = Start-Process core/tests/sessionEndBusy/bin/sessionEndBusy.exe -ArgumentList 'idle-log 0 inf C:\Users\Public\device-239-idle.log' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\7.log"; $p.ExitCode`. Expect exit 0; stdout and `C:\Users\Public\device-239-idle.log` contain `App final exit line 1` and `exit: clean`, and stdout contains `App final stdout exit line`. This exercises the session-ending seam, not real OS logout.
8. **[HUMAN] LAST: real session end.** Complete steps 1–7, 9 and 10 first. Run `$p = Start-Process $probe -ArgumentList 'session' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\8.log"; $p.ExitCode`. Save other work. Screen shows public/native mapping; T cycles 2/5/10 s watchdog timeouts. In separate launches log out/restart/shut down while idle at each setting; then press D and log out during its timestamped 30 s main-thread block. After login inspect `C:\Users\Public\device-239-probe.log`: idle retains App exit/cleanup and framework `exit: clean`; busy OS termination may skip those and atexit. Measure OS deadlines separately. exitRequested is the public observation of WM_QUERYENDSESSION, accepted cleanup represents WM_ENDSESSION; native query/cancel/commit itself is not exposed. Hidden Runtime HWND after all user windows close, headless ending and artificial idle delays are not implemented by this public probe; record those UNVERIFIED. Never infer the exit code of a GUI executable from `$LASTEXITCODE`.
9. **[HUMAN]** Run `$p = Start-Process $probe -ArgumentList 'keepawake' -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$env:USERPROFILE\device-239-win\9.log"; $p.ExitCode`. Press K to toggle setKeepScreenOn; screen and Public log show each value. Observe display power inhibition while ON and release when OFF or after normal close (expect 0). The probe does not measure console code-page restoration, headless ending, or a retired first window; record those UNVERIFIED. Do this before step 8's real logout.
10. **[HUMAN] Web.** Serve the output of `--web-only` locally with `python3 -m http.server 8000` from the repository root, open the generated example HTML in Edge/Chrome, and observe drawing; hide and restore the tab and exercise browser context/device loss in both WebGL2 and WebGPU configurations. Playback resized above the device texture limit must emit a playback error rather than silently lose the texture; pthread keep-screen-on must be applied and released. The repository does not provide a verified browser command or complete interactive fixture for all these cases in this handoff: record each unexecuted scenario UNVERIFIED in `~/device-239-win/10.log`. Missing browser/backend is SKIP; installing one needs owner OK. Android smoke has no assigned device and is UNVERIFIED.

Report back one line per step: `<step#> PASS|FAIL|SKIP|UNVERIFIED <log path> <one-line note>`. An unavailable platform feature is SKIP; only a wrong result is FAIL; anything not run is UNVERIFIED, never PASS. For mixed results give separate lines with the same step number.

## trussc-gdm — Linux X11, real display

Use the real logged-in X11 display, not Xvfb. Do not install anything without the owner's OK. Start in a clean clone at the repository root:

```sh
git fetch origin probe/an2-device && git checkout --detach origin/probe/an2-device
git rev-parse HEAD
mkdir -p "$HOME/device-239-gdm"
trusscli update -p core/tests/an2DeviceProbe --tc-root "$PWD" --ide cmake
cmake -S core/tests/an2DeviceProbe -B core/tests/an2DeviceProbe/build-linux -DTRUSSC_DIR="$PWD/core" -DCMAKE_BUILD_TYPE=Release
cmake --build core/tests/an2DeviceProbe/build-linux --config Release
PROBE=core/tests/an2DeviceProbe/bin/an2DeviceProbe
python3 examples/build_all.py --core-tests-only --include-daily 2>&1 | tee "$HOME/device-239-gdm/build-core.log"
python3 examples/build_all.py --addon-tests-only --include-daily 2>&1 | tee "$HOME/device-239-gdm/build-addon.log"
```

The suite builds and runs tests; inspect its summary. If trusscli or another prerequisite is missing, installing it needs owner OK; record UNVERIFIED.

1. **[HUMAN]** Build and launch the existing two-context example:

   ```sh
   trusscli update -p examples/windowing/imguiWindowsExample --tc-root "$PWD" --ide cmake
   cmake -S examples/windowing/imguiWindowsExample -B examples/windowing/imguiWindowsExample/build-linux -DCMAKE_BUILD_TYPE=Release
   cmake --build examples/windowing/imguiWindowsExample/build-linux --config Release
   examples/windowing/imguiWindowsExample/bin/imguiWindowsExample 2>&1 | tee "$HOME/device-239-gdm/1.log"
   ```

   Click “open second window”; move the pointer across sliders and text in both windows. Expect the hovered window's ImGui context alone to control the cursor, without fighting or flicker. Record what happened in the log.
2. Run `core/tests/allCoreTests/bin/allCoreTests appEventDelivery --display > "$HOME/device-239-gdm/2.log" 2>&1; echo $?`. Expect exit 0 and all executed checks PASS. Include the core/addon build summaries in the report; a build failure is not a cursor result.

3. **[HUMAN]** Run `"$PROBE" cursor > "$HOME/device-239-gdm/3-cursor.log" 2>&1; echo $?`; press 1–5/B/U/H/S in each window and move pointer across both. Screen state and actual cursor must agree app-wide. Run `"$PROBE" drop > "$HOME/device-239-gdm/3-drop.log" 2>&1; echo $?`; drop files on each window, checking only that receiver ID/count/all paths. Close both windows after each run, expect 0.
4. **[HUMAN]** Run `"$PROBE" close > "$HOME/device-239-gdm/4.log" 2>&1; echo $?`. × on quitOnClose A first vetoes (both stay, auto-off), second accepts. Relaunch for C in A (no closeRequested), then B first and last-window A veto/accept. V rearms. Screen shows last key receiver; expect exit/cleanup count=1 per App, process exit once, exit 0. Send `~/device-239-probe.log` for all modes.

No logout step on Linux: TrussC has no Linux session receiver by design, so there is no TrussC session-end behaviour to verify here.

Report back one line per step: `<step#> PASS|FAIL|SKIP|UNVERIFIED <log path> <one-line note>`. An unavailable platform feature is SKIP; only a wrong result is FAIL; anything not run is UNVERIFIED, never PASS. For mixed results give separate lines with the same step number.

## RasPi — Raspberry Pi 5, labwc / Xwayland / GLES3

Run in the real graphical session, with a connected display, from a clean clone at the repository root. Do not install anything without the owner's OK; missing tools are UNVERIFIED. Ensure the existing configuration selects GLES3 and record the backend in the logs.

```sh
git fetch origin probe/an2-device && git checkout --detach origin/probe/an2-device
git rev-parse HEAD
mkdir -p "$HOME/device-239-pi"
trusscli update -p core/tests/an2DeviceProbe --tc-root "$PWD" --ide cmake
cmake -S core/tests/an2DeviceProbe -B core/tests/an2DeviceProbe/build-linux -DTRUSSC_DIR="$PWD/core" -DCMAKE_BUILD_TYPE=Release -DTC_GRAPHICS_BACKEND=GLES3
cmake --build core/tests/an2DeviceProbe/build-linux --config Release
PROBE=core/tests/an2DeviceProbe/bin/an2DeviceProbe
python3 examples/build_all.py --core-tests-only --include-daily 2>&1 | tee "$HOME/device-239-pi/build-core.log"
python3 examples/build_all.py --addon-tests-only --include-daily 2>&1 | tee "$HOME/device-239-pi/build-addon.log"
cmake -S core/tests/nativeWindow -B core/tests/nativeWindow/build-linux -DCMAKE_BUILD_TYPE=Release -DTC_GRAPHICS_BACKEND=GLES3
cmake --build core/tests/nativeWindow/build-linux --config Release
# Rebuild the hot-reload host AFTER the addon sweep (the sweep relinks libTrussC; a stale host exits 65 "host older than TrussC"):
cmake --build core/tests/hotReloadLifecycle/build-linux --config Release
```

These suite commands build and run tests. If GLES3 is not selected by the existing configuration, do not treat a different backend as the GLES3 result. Installing a tool or driver needs owner OK.

1. Run `core/tests/nativeWindow/build-linux/nativeWindow --display > "$HOME/device-239-pi/1-native.log" 2>&1; echo $?`, and `core/tests/allCoreTests/bin/allCoreTests windowTick --display > "$HOME/device-239-pi/1-tick.log" 2>&1; echo $?`, followed by `core/tests/allCoreTests/bin/allCoreTests eglWindowSurfaces --display > "$HOME/device-239-pi/1-egl.log" 2>&1; echo $?`. Expect exit 0 and all executed assertions PASS. Inspect the logs for main/secondary cadence, hide/resume and timer handoff, interval-zero swaps and no duplicate tick on source reversal. If a particular behavior is not asserted by these modes, mark it UNVERIFIED. Real GLX and EGL are separate paths: this GLES3 run does not establish physical GLX coverage.
2. Run `core/tests/allCoreTests/bin/allCoreTests perWindowShadows --gpu-check > "$HOME/device-239-pi/2.log" 2>&1; echo $?`. Expect exit 0 and 16 comparisons with zero pixels outside tolerance 2, including shadow-layer readback. Other render-boundary coverage should be collected from the real-display suite; mark any GLX-only or unexecuted path UNVERIFIED.
3. Run `core/tests/hotReloadLifecycle/bin/hotReloadLifecycle --reload-check > "$HOME/device-239-pi/3-reload.log" 2>&1; echo $?` and repeat with `--c4-checks` to a separate log. Expect exit 0, `--reload-check: OK`, `C4 substrate: PASS`, warning naming the real `TC_HOT_RELOAD(tcApp)` and file on second/third window, and retired module remains mapped. **[HUMAN]** Run `"$PROBE" close > "$HOME/device-239-pi/3-manual.log" 2>&1; echo $?`. A is quitOnClose, B ordinary. × on A first vetoes (both remain, auto-off); repeat accepts. Relaunch for C in A (code close bypasses closeRequested), and again for B first then last-window A veto/accept. V rearms; screen shows last key receiver. Expect exit 0, each App exit/cleanup once and process exit once in `~/device-239-probe.log`. Run `"$PROBE" cursor > "$HOME/device-239-pi/3-cursor.log" 2>&1; echo $?`, pressing 1–5/B/U/H/S in both and moving pointer between them; both screens/log must agree. Run `"$PROBE" drop > "$HOME/device-239-pi/3-drop.log" 2>&1; echo $?`, drop files on both and check receiver ID/count/all paths. Close both windows after each run. **[HUMAN] LAST, after step 4:** run `"$PROBE" session > "$HOME/device-239-pi/3-session.log" 2>&1; echo $?`. T cycles timeout 2/5/10 s. Test real session logout idle and during D's logged 30 s main-thread block in separate launches, inspect durable log after login. Linux logout delivery depends on session manager; report observed exitRequested/exit/cleanup/final clean line, or UNVERIFIED, without inferring native callbacks. Send the durable log with stdout logs.
4. Run `core/tests/allCoreTests/bin/allCoreTests mediaDecode > "$HOME/device-239-pi/4-media.log" 2>&1; echo $?`, `core/tests/allCoreTests/bin/allCoreTests videoDecodeLimits > "$HOME/device-239-pi/4-video.log" 2>&1; echo $?`, and `core/tests/imageDecodeBounds/bin/imageDecodeBounds > "$HOME/device-239-pi/4-image.log" 2>&1; echo $?`. Expect exit 0 and executed checks PASS. Check specifically the GIF thread stack, Vulkan frames and surfaceless EGL results; where the relevant platform feature is unavailable record SKIP with its reason. Do not infer that those individual cases ran merely from process exit 0; report them UNVERIFIED if no corresponding verdict is present.

Report back one line per step: `<step#> PASS|FAIL|SKIP|UNVERIFIED <log path> <one-line note>`. An unavailable platform feature is SKIP; only a wrong result is FAIL; anything not run is UNVERIFIED, never PASS. For mixed results give separate lines with the same step number.

## Dropped items (for the auditor's record only, not forwarded)

- Windows "bad/non-PE guest image load shows no system dialog" via the loader error-mode wrapper (wrapper removed in C4 fu2; only the test-side SetErrorMode remains, covered above).
- "A closed / B alive reload", tcHotReloadAppTypeKey, multi-attachment reload (multi-window reload is unsupported).
- Session freeze, ForcedBusy, constructor-interrupted ending (removed in C3 trim).
- Crash handler / crash report (removed, #1143 + C3 trim).
