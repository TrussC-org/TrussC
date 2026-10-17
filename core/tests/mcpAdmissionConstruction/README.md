# MCP admission construction

Default mode is the existing headless admission/model test (Linux framebuffer
link wrappers). `--gpu-check` is the existing real-framebuffer rejection test.
`--gpu-check-resize` uses native windows and `setWindowSize`, renders three
large frames per cycle (requests 3840×2160 physical; logs the actual size),
then shrinks to 320×240 logical. It waits up to 60 rendered frames for the
real framebuffer to reach the requested physical size, measuring after that
window's tick refreshes its cache. The following main-loop prelude sends
`tc_save_screenshot` through the HTTP request queue before drawing. It checks
deferral, working rejections, PNG dimensions, frame-end drawable cleanup and
released working credits and the sole screenshot admission slot. It repeats three times per window, including a real
secondary on macOS/Windows/Linux; unsupported platforms print SKIP. The final
`RESIZE` line reports counts; any failed assertion or incomplete run exits 1.
Close other windows that could cover the test windows; secondary rendering
pauses when the OS reports occlusion. Such windows skip automatically; allow
the test to finish.

The save-success budget is derived independently from requested logical size
and measured DPI: `320*scale * 240*scale * 64 + 4 MiB` (9,109,504 bytes at
DPI 1; 23,855,104 at DPI 2). After every saved frame, three screenshot tools
must reject at budgets just below physical admission: save uses 64 bytes/pixel
plus 1 MiB, get/analyze use 8 bytes/pixel. The rejection limit uses requested
logical size times measured DPI in resize mode, and actual readback pixels in
GPU mode; neither uses `captureWindowSize` to calculate its limit.

The `tc_get_screenshot` rejection probe requests a 16-pixel thumbnail so
subsequent encoding fits even with a logical-size admission bug; a late encoder
rejection cannot falsely satisfy the admission assertion. Successful image
responses are recognized by the test helper as success.

A full two-window resize run expects **160/160** assertions, **6** cycles,
**6** accepted saves and **12** pointer checks. GPU mode expects **18/18**.
Default mode expects **188/188 on Linux**, **142/142 on macOS/Windows**.

A window with no rendered frame for **3 seconds** (`noFrameLimit`) prints
`SKIP: window <n> renders no frames (occluded or hidden)` and skips its
remaining cycles. The main window polls this wall-clock deadline even if the
secondary never receives its first tick or `setup()`, before the 45-second
global failure timeout. This is separate from a window that renders but
refuses to resize: that resize timeout prints
`SKIP: window resize not supported here (requested WxH, measured WxH)`
for that window and skips its remaining resize-dependent checks. The window
index and remaining cycle count are logged separately. A grow that cannot
produce a framebuffer larger than the small target also skips after 60 frames;
OS-constrained large sizes remain allowed. No native dimensions are faked or
manually refreshed. Each completed cycle contributes 26 assertions; a full
run has four shared assertions. A window skipped before its first cycle omits
78 assertions. Linux's main-window `setWindowSizeLogical()` is a stub, so Linux
expects **82/82**, **1 window SKIP**, **3** secondary cycles, **3** accepted saves
and **6** pointer checks at either DPI. Air/Windows expect **160/160** when both
windows resize, **82/82** if one window refuses all resizing, or **4/4** if both
refuse; all exit 0 with explicit SKIPs. On the Air in clamshell mode with a
dummy plug, the permanently occluded secondary is expected to give **82/82 +
1 SKIP**, **3** main-window cycles, **3** accepted saves and **6** pointer checks
at each supported DPI, exiting 0 instead of hitting the global timeout.
This is an expectation awaiting an Air rerun, not measured device evidence.
Secondary-window unsupported platforms omit its creation assertion (and its
78 cycle assertions).

Linux rerun (2026-10-18, software Mesa/Xvfb): default **188/188**;
resize DPI 1 and 2 each **82/82 + 1 resize SKIP**. With a native X11 cover
suppressing secondary frames, each DPI gives **4/4 + 2 SKIPs** (Linux main
resize refusal plus secondary no-frame SKIP), finishing in **3.50 s / 3.65 s**
instead of 45 s. All five runs have **0 failures**, exit **0**.

Build from the repository root with the usual generated project:

```sh
tools/bin/trusscli update -p core/tests/mcpAdmissionConstruction --tc-root "$PWD" --ide cmake
cmake -S core/tests/mcpAdmissionConstruction -B core/tests/mcpAdmissionConstruction/build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build core/tests/mcpAdmissionConstruction/build-linux -j4
```

Linux GL with Xvfb (run from repository root; `xrdb` sets the real X11 DPI):

```sh
core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction
LIBGL_ALWAYS_SOFTWARE=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
xvfb-run -a -s '-screen 0 4096x2304x24 -noreset' sh -c '
  printf "Xft.dpi: 96\n" | xrdb -merge
  core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction --gpu-check --dpi=1
  core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction --gpu-check-resize --dpi=1
  printf "Xft.dpi: 192\n" | xrdb -merge
  core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction --gpu-check --dpi=2
  core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction --gpu-check-resize --dpi=2
'
```

Air/macOS (Retina display, from repository root after building the test):

```sh
core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction.app/Contents/MacOS/mcpAdmissionConstruction
core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction.app/Contents/MacOS/mcpAdmissionConstruction --gpu-check --dpi=1
core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction.app/Contents/MacOS/mcpAdmissionConstruction --gpu-check --dpi=2
core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction.app/Contents/MacOS/mcpAdmissionConstruction --gpu-check-resize --dpi=1
core/tests/mcpAdmissionConstruction/bin/mcpAdmissionConstruction.app/Contents/MacOS/mcpAdmissionConstruction --gpu-check-resize --dpi=2
```

Windows PowerShell (from repository root after building the test):

```powershell
& .\core\tests\mcpAdmissionConstruction\bin\mcpAdmissionConstruction.exe
& .\core\tests\mcpAdmissionConstruction\bin\mcpAdmissionConstruction.exe --gpu-check --dpi=1
& .\core\tests\mcpAdmissionConstruction\bin\mcpAdmissionConstruction.exe --gpu-check-resize --dpi=1
# Set the test display's Windows Scale to 200% before the DPI 2 commands.
& .\core\tests\mcpAdmissionConstruction\bin\mcpAdmissionConstruction.exe --gpu-check --dpi=2
& .\core\tests\mcpAdmissionConstruction\bin\mcpAdmissionConstruction.exe --gpu-check-resize --dpi=2
```

`--dpi=2` requests high-DPI rendering and asserts a real 2× framebuffer.
On Windows at measured scale 1, both display modes print
`SKIP: DPI 2 needs a display scaled to 200% (measured 1.00)` and report **0/0**
assertions, exit 0. This is not DPI-2 coverage; use 200% scaling to execute it.
Other unexpected scales fail. The Air's built-in Retina supplies actual 2×.
