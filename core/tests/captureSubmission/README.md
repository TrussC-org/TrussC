Run `allCoreTests captureSubmission --gpu-check [--msaa4]` on GL/D3D11/Metal
(Xvfb/Mesa works on Linux). The main-window lane always runs, with the same
prefix, park/resume, onRender and afterFrame assertions on every backend.
On GL/D3D11 with secondary-window support, a begun swapchain and a nested MSAA
Fbo park across another window's present, then resume with LOAD. Otherwise
the main lane parks its own pass directly. Check prefix/suffix pixels,
source sizes, fixed-size recorder takes and native pack/read state. Screenshot
cases also cover the first main/secondary frame's public clear/setColor/drawRect
sequence without an internal pass begin, new CPU drawings after parking,
and an onRender listener inside present (#884). On GL/GLES
the explicit destination seam copies the full swapchain through the shared
end-pass resolve path under a 1x1 scissor. It does not install completed-frame
snapshots. The optional recorder integration reports codec absence as a skip.

On Windows add `--diag` to print every secondary D3D11 capture's context,
framebuffer extent, saved swapchain extent/format/samples and view identities,
selected RTV/texture identity and actual DXGI texture description, reconciled
extent, returned Pixels extent, and the active recorder's input/native packed
layout. `recorder=0x0` means no recorder is active for that capture. The test
alone parses this flag and installs the internal observer; normal production
capture emits no diagnostic output. For example, from the combined runner's
directory in PowerShell:

```powershell
.\allCoreTests.exe captureSubmission --gpu-check --diag
.\allCoreTests.exe captureSubmission --gpu-check --msaa4 --diag
```

Both A and B use four samples with `--msaa4`. Windows also checks B's native
views before its first tick and rejects fallback to A when B's saved view is
absent. The manual coordinator runs from the draw event under the framework's
Draw scope, before the Node tree's matrix push, since its explicit present()
calls finish frames and reset their stacks. Stack warnings fail the test.

Only the secondary foreign-window cases skip on Metal: a secondary window's
drawable can't be acquired outside its native tick. Platforms where
`internal::supportsSecondaryWindows()` is false print
`SKIP: secondary windows unsupported on this platform` without calling
`createWindow`. Each lane skip prints once; skipped cases add no assertions or
failures. The screenshot Warning/Error counter belongs to the main window,
so B's refusals do not inflate the main lane's warning-once check. Codec skips
also print once per recorder category. Executed assertions determine the exit
code through the display-test cleanup, including on macOS.

From the repository root, run on the Air with Metal API and GPU validation:

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./core/tests/allCoreTests/bin/allCoreTests.app/Contents/MacOS/allCoreTests captureSubmission --gpu-check
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./core/tests/allCoreTests/bin/allCoreTests.app/Contents/MacOS/allCoreTests captureSubmission --gpu-check --msaa4
```

On the Pi, use its native display and the GLES3/EGL build:

```sh
./core/tests/allCoreTests/bin/allCoreTests captureSubmission --gpu-check
./core/tests/allCoreTests/bin/allCoreTests captureSubmission --gpu-check --msaa4
```

The main-only Metal lane does not cover foreign native ticks. For that device
protocol, use real native ticks with Metal API validation and install
`sg_tc_mtl_set_drawable_trace()` (a framework seam, not app API) while running the following protocol (actions:
0 register, 1 authorize, 2 present, 3 cancel, 4 non-presenting prefix submit):

- A begins a real acquired swapchain pass; suspend A through a foreign native
  tick/modal loop. B finalizes and commits repeatedly, including an upload-only
  boundary. A has zero authorization/presentation events; B presents once per
  completed frame. Resize A or change its screen while parked; drain a local
  autorelease pool and commit B beyond the pass-descriptor retirement age.
  A keeps the same drawable, depth/MSAA textures, frame and generation.
- Resume A with LOAD, draw its suffix/final onRender, finish A, then commit.
  Require one A authorization followed by one A presentation. Repeat samples
  1/4, explicit discard/close, nested Fbo/fixed tops and stale authorization.
- Capture synchronously after a pass begins and from a nested foreign update.
  Require park/end -> prefix submit (action 4) -> staging -> completion, with no
  presentation or CPU completion from the capture boundary. For async capture,
  stage the selected source before the next write and check native completion
  ownership through stop, close and restart refusal.
- For #884, repeat synchronous grabScreen from the first App::draw before any
  native pass has opened (clear/setColor/drawRect), after parking with new CPU
  drawings, and from an onRender
  listener inside present, on both main and secondary native ticks, including
  after parking another window's pass. Require staging submission before the
  wait, LOAD resume, and unchanged owner presentation counts. Missing native
  sources must return false with one Error. On macOS, inject a stalled slot/completion:
  each capture wait must fail within its ten-second bound, leave the pass
  parked and report one Error. Allow late completion after the caller's Pixels
  and stack have gone away; require owned-result cleanup without caller access.
  Check resource accounting for late completions: the original budget/save slot
  remains owned through native cleanup, and an abandoned result allocates no
  Pixels when the delayed callback arrives.

Completed-frame marker stability after presentation is deferred with Q2; this
part keeps the existing native capture sources and their timing limitations.
Pi must exercise the explicit screen resolve on native V3D (including the
pre-resolve flush Notice); llvmpipe is a negative control. Windows must run the
D3D11 suites with its debug layer. iOS/Android/Web still need their supported
backend runtime checks; a desktop result is not device evidence.

`local.cmake` (also the combined runner when this fixture is selected) enables
`SOKOL_TC_TEST_HOOKS` for the core implementation; this fixture enables the
declarations before including TrussC. Custom native fixtures must enable it in
both translation units. Production builds must leave it undefined.

The Linux setup also checks #320 before a secondary window's first tick:
initialized DPI, immediate `Window::setSize`, and logical/pixel-perfect `setWindowSize`.
Set `Xft.dpi: 192` with `xrdb -merge` under Xvfb to exercise scale 2.0
(use Xvfb `-noreset` to retain the resource between clients); the 96 DPI run
also checks the 1.0 path. The probe queries actual GLX drawable dimensions.

Public grabScreen phase contract: the fixture's update() rejects repeated calls
with one Warning per window (shared with setup rejection), before touching a
backend. Prefix capture checks run from draw(), including the explicit secondary
coordinator's Draw scope. The real afterFrame listener checks the finished frame
including a later onRender overlay; draw's prefix excludes that overlay. ImGui
finished-frame coverage is in addons/tcxImGui/tests-renderTarget (record/compare).
