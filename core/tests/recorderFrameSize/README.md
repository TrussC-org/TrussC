# Recorder frame size device check

`--device-check` needs a visible desktop window and a working native H.264
encoder. It runs on macOS (Metal/AVFoundation), Windows (D3D11/Media Foundation),
Linux GLCORE and Raspberry Pi GLES3 (GStreamer). Web, iOS and Android print SKIP.
Leave the test window visible until it exits. The no-argument test stays headless;
the existing Linux GLCORE `--gpu-check` still checks X11 resizing and GL pack state.

The headless take uses 64x64 for desktop H.264 encoder compatibility; its frame
fixtures and byte counts are derived from that size. Layout and overflow checks
run before opening the encoder. An explicit unsupported-media-type open failure
prints `SKIP: encoder rejected a 64x64 take (<result/HRESULT text>)` and skips
only encoding checks on non-Linux platforms. Other open failures are FAILs.
Linux/GStreamer must open the take and run all 16 headless checks.

Before opening the screen take, device mode calls `setWindowSize(W, H)` with
`W=320`, `H=240` and records the framebuffer extent after it settles. It then
uses the same API to request `(W, H*3/2)`, `(W*3/2, H)` and `(W, H)`.
No DPI-to-pixel prediction is used: taller means framebuffer height grew with
width unchanged relative to the recorded take; wider means width grew with
height unchanged. Returning must restore the exact recorded extent. This also
handles Windows at **200% display scaling**, where the current Win32 logical
resize implementation does not multiply its arguments by DPI. Run that scaling
setting explicitly; the opening framebuffer size is not assumed to be the take.

Device mode uses the main window on every platform. On desktop Linux,
`setWindowSizeLogical()` is a no-op, so **the device resize part SKIPs** while
matching window recording and synchronous Fbo checks run. Pi's main resize is
also a stub. Real Linux X11 taller/wider resizing, mismatch rejection and resume
are covered by `--gpu-check`; run both modes. No native resize or GL API is used
in device mode. Window capture is asynchronous on Metal; Windows uses synchronous
D3D11 capture. Live accepted counts hold the recorder lock, and successful stop
drains pending captures.

Initial settling and each resize allow at most 240 rendered frames or four
seconds. Settling needs 12 unchanged frames and 0.25 seconds. Initial matching
captures have the same maximum wait; the whole device app has a 25-second
deadline. An unchanged resize request prints SKIP for that step. If no request
changes the framebuffer, one SKIP line covers the entire resize part. A changed
but unexpected extent is printed and fails the direction assertion; it must
still be rejected as a mismatch. An effective mismatch that is not rejected,
a return that never reaches the take after resizing, or matching captures that
do not resume is a FAIL. Once the return reaches the take, the resume assertion
is mandatory, including after a skipped step. Exit 0 requires an effective
mismatch and verified resume unless the platform made no framebuffer changes.
Exactly one layout Warning is required for a mismatched take (zero for an
unchanged take). A separate `VideoWriter::addFrame(fbo)` take checks synchronous
mismatch refusal/counting, matching-frame acceptance and exactly one Warning.
Recording Errors, incomplete runs and exhausted global deadlines fail the run.

After successful finalization, `RESULT screen ...` and `RESULT fbo ...` report
take dimensions, accepted/rejected counts, layout warnings, a JSON-quoted file
path, file bytes and `fileSize=WxH`. Files are opened through `VideoPlayer`;
an unavailable decoder prints `fileSize=unavailable` and skips only that
dimension assertion. Optional player-load diagnostics do not count as recording
Errors. Decoded dimensions, when available, must equal the take dimensions.
If screen stop or Fbo writer close fails, the run records FAIL and prints
`RESULT <kind> status=stop-failed file=...`, without reading writer counters or
reporting file bytes/dimensions on that branch. Cleanup retains the output
directory and prints its path so pending completion work can still write there.
Otherwise temporary recordings are deleted after reporting unless `--keep dir`
retains both MP4s in a unique subdirectory of `dir` (created if necessary).

## Build and run

Run these from the repository root with the usual TrussC platform dependencies
installed. Substitute your installed `trusscli` path if necessary.

Linux desktop GLCORE or Raspberry Pi OS desktop GLES3 (CMake detects the Pi):

```sh
tools/bin/trusscli update -p core/tests/recorderFrameSize --tc-root "$PWD" --ide cmake
cmake -S core/tests/recorderFrameSize -B core/tests/recorderFrameSize/build-device -DCMAKE_BUILD_TYPE=Release
cmake --build core/tests/recorderFrameSize/build-device -j4
cd core/tests/recorderFrameSize
bin/recorderFrameSize --device-check
bin/recorderFrameSize --device-check --keep /tmp/recorder-device-results
```

Linux software GLCORE under Xvfb (after building, from the test directory):

```sh
LIBGL_ALWAYS_SOFTWARE=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
xvfb-run -a -s '-screen 0 1280x1024x24' bin/recorderFrameSize --device-check --keep /tmp/recorder-device-results
LIBGL_ALWAYS_SOFTWARE=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
xvfb-run -a -s '-screen 0 1280x1024x24' bin/recorderFrameSize --gpu-check
```

macOS / MacBook Air (use the Retina display; build Debug for runtime validation):

```sh
tools/bin/trusscli update -p core/tests/recorderFrameSize --tc-root "$PWD" --ide cmake
cmake -S core/tests/recorderFrameSize -B core/tests/recorderFrameSize/build-device -DCMAKE_BUILD_TYPE=Debug
cmake --build core/tests/recorderFrameSize/build-device -j4
cd core/tests/recorderFrameSize
bin/recorderFrameSize.app/Contents/MacOS/recorderFrameSize --device-check
bin/recorderFrameSize.app/Contents/MacOS/recorderFrameSize --device-check --keep /tmp/recorder-device-results
```

Windows PowerShell (Visual Studio developer shell; also run at 200% display
scaling for DPI coverage):

```powershell
tools/bin/trusscli.exe update -p core/tests/recorderFrameSize --tc-root "$PWD" --ide cmake
cmake -S core/tests/recorderFrameSize -B core/tests/recorderFrameSize/build-device -G "Visual Studio 17 2022" -A x64
cmake --build core/tests/recorderFrameSize/build-device --config Release --parallel 4
cd core/tests/recorderFrameSize
bin/recorderFrameSize.exe --device-check
bin/recorderFrameSize.exe --device-check --keep "$env:TEMP/recorder-device-results"
```

With a combined core build, use `bin/allCoreTests recorderFrameSize --device-check
[--keep dir]` instead (or the equivalent `.app`/`.exe` path).

The default headless sweep supplies no arguments, so it does not enter this
mode. Daily Linux display invocations come from `display-test`: each line uses
`{test}` for the standalone executable or combined executable plus test name.
The existing marker remains `{test} --gpu-check`; `--device-check` is an explicit
device-lab invocation and is not added to the headless or daily sweep. See
[`core/tests/README.md`](../README.md#display-modes-display-test-marker-daily-linux-only).
