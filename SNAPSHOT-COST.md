# #821 PR 5 / Q2: completed-frame snapshot cost and capture policy

Analysis and probe only, 2026-10-18. Base: `next/render` commit `a711686f05dcd8f8dcb93aa919efc0c9f572e024` (the checkout matches). Read against `PROPOSAL-821-PR5.md`, especially “Completed-frame strategy requiring approval.” No production implementation, approval, commits, or native Pi/Air/Windows validation is claimed. Probe sources and raw evidence are untracked under `tools/probes/snapshot-cost/`; builds live in `/tmp`.

## Recommendation and the unavoidable choice

**Do not adopt always-on snapshots as the default. The simplest predictable zero-idle-cost policy is (c): a per-window explicit completed-frame-retention setting, default off, chosen before rendering for apps that need arbitrary synchronous screen capture.** Off means no snapshot allocation and no per-frame snapshot copy. Keep ordinary rendering/presentation independent of whether capture retention is enabled or succeeds. An opt-in capture allocation failure must report a capture failure, not stop the exhibition from presenting. This changes the proposal's “snapshot failure prevents presentation” coupling.

For convenience without a setting, (b) is viable if the owner explicitly accepts a cold first `grabScreen` returning false and the permanent cost after first use. It preserves the convenient scheduled screenshot/recorder workflows with earlier registration, but one MCP screenshot then silently imposing copies forever is a poor default for a long-running exhibition. Automatically disarming after a request/recording would avoid that permanent tax but introduces a different request-scoped policy and recurrent cold misses; it is not literal “arm on first use.”

**There is no transparent zero-idle-copy solution that also guarantees an immediate copy of an arbitrary window's previous completed frame on its very first capture call at any phase.** That image must already exist somewhere. A prefix submit can make current encoded writes executable; it cannot recover a frame whose presentation/discard or next-frame overwrite already happened. If the owner requires the proposal's full first-call contract without any opt-in, (a), or a separately measured persistent render-to-texture architecture, remains necessary. This report does not silently weaken that contract.

## Incremental memory and traffic

The declared native screen formats are **four bytes per pixel**, including packed RGB10A2 (10+10+10+2 = 32 bits, not 64). Preserve their native bits; an RGBA8 conversion would lose the 10-bit source precision. **GLES caveat found by measurement:** the Linux/Pi EGL path requests zero alpha by default, so its actual attachment can be RGB8 even though Sokol reports RGBA8. The final probe retains RGB8 for that surface to make the MSAA resolve format-compatible; RGB8 has a three-byte logical payload and may be padded to four bytes/pixel by the driver. Do not equate the reported Sokol format with inspected native attachment storage.

| Backend / window at the base | Source and retained format | Bytes/pixel |
| --- | --- | ---: |
| Linux desktop GL | reported RGBA8 / `GL_RGBA8` snapshot | 4 |
| Pi/Linux GLES3 | reported RGBA8; actual default EGL RGB8 when alpha=0, retained as `GL_RGB8`; RGBA8 if alpha=8 | 3 logical for RGB8; 4 for RGBA8; allocator-dependent padding |
| macOS Metal main; iOS Metal | `MTLPixelFormatRGB10A2Unorm` | 4 |
| macOS Metal secondary | `MTLPixelFormatBGRA8Unorm` | 4 |
| Windows D3D11 main | `DXGI_FORMAT_R10G10B10A2_UNORM` | 4 |
| Windows D3D11 secondary | `DXGI_FORMAT_B8G8R8A8_UNORM` | 4 |
| Android GLES3 | reported RGBA8; validate actual EGL surface compatibility | 4 |

Source anchors: `core/include/sokol/sokol_app_tc.h` main Metal format 3493, main layer capture capability 3535, secondary format 3713, iOS format 4966, D3D main 7668 and secondary 7832, GL format 11576 and GLES format branch 15366; secondary adapters `core/platform/mac/tcWindowMac.mm:41` and `core/platform/linux/tcWindowLinux.cpp:44`. EGL config alpha selection is at `sokol_app_tc.h:15065`–15103. Local attachment inspection found RGB=8/8/8, alpha=0. Web has no native retained-snapshot proposal; existing unsupported Pixels capture/browser download behavior remains separate.

For physical framebuffer size W×H, format B bytes/pixel, and N windows:

- One retained single-sample image: **W × H × B bytes/window**; N equal windows multiply by N. Use physical pixels, not Retina points.
- One logical full-frame copy: W×H×B source bytes plus the same destination bytes. At F rendered frames/s: **2 × W × H × B × F × N bytes/s** of logical read+write traffic.
- These are payload calculations, **not measured heap allocation or external-memory bandwidth**. Driver alignment, tiling, compression, cache reuse, render-pass stores, tile flushes and MSAA can change both actual allocation and traffic. The snapshot remains single-sample even at MSAA 4; existing swapchain/MSAA/depth allocations are excluded. In-flight readback staging, a pinned resize generation and encoder buffers add further memory.

| Physical size (four-byte retained formats) | Extra bytes/window | MiB/window | Logical read+write per copied frame | Logical read+write at 60 fps/window |
| --- | ---: | ---: | ---: | ---: |
| 1920×1080 | 8,294,400 | 7.910 | 16,588,800 B | 0.995328 GB/s |
| 2560×1600 | 16,384,000 | 15.625 | 32,768,000 B | 1.966080 GB/s |
| 2940×1912 | 22,485,120 | 21.443 | 44,970,240 B | 2.698214 GB/s |
| 3840×2160 | 33,177,600 | 31.641 | 66,355,200 B | 3.981312 GB/s |

For an inspected RGB8 GLES source retained in RGB8, the corresponding **logical payload** figures are below. Actual GPU allocation/traffic may use four-byte texels; use the four-byte table as a planning allowance unless the native allocator is measured. No claim of a three-byte physical GPU allocation is made.

| Physical size | RGB8 payload/window | MiB | Logical read+write at 60 fps/window |
| --- | ---: | ---: | ---: |
| 1920×1080 | 6,220,800 B | 5.933 | 0.746496 GB/s |
| 2560×1600 | 12,288,000 B | 11.719 | 1.474560 GB/s |
| 2940×1912 | 16,863,840 B | 16.083 | 2.023661 GB/s |
| 3840×2160 | 24,883,200 B | 23.730 | 2.985984 GB/s |

MiB = 2²⁰ bytes; GB/s = 10⁹ bytes/s. Two four-byte-format 1080p windows add **16,588,800 bytes (15.820 MiB)** and **1.990656 GB/s** logical traffic at 60 rendered fps each. At 4K, the proposal's 1.990656 GB/s figure counts only source reads; including writes doubles it. Do not translate these figures into GPU milliseconds without measurement. On tile GPUs a new resolve/store can matter more than the nominal copy payload.

## Probe and measurement method

`tools/probes/snapshot-cost/` contains a real TrussC `App`, a probe-only native Sokol implementation TU, a CMake build, and `run.py`. It links the base TrussC core. The executable provides Sokol's symbols, so the core archive's stock implementation member is not extracted. This exposes the renderer command buffer inside the probe without editing the fork or production sources.

The scene uses the ordinary deferred TrussC renderer: a background, 96 colored panels, 32 animated translucent circles and bitmap text. Each window renders the same workload. The two-window run uses a barrier between blocks: both windows stay in the same off/on mode, and a faster window continues rendering without collecting extra samples until the slower window has finished that block. An `onRender` listener consumes the final draws and ends that window's pass; it then optionally copies the actual acquired swapchain into one reusable native texture, before ordinary commit/presentation. No screenshot encoding, disk I/O or CPU full-image readback is timed.

- GL/GLES3: same-extent `glBlitFramebuffer`, RGBA8 destination FBO (RGB8 for an inspected alpha-free GLES source), source framebuffer from the recorded swapchain; MSAA resolves directly to that destination. The GL/GLES 4× case includes this additional resolve while the platform still performs its normal presentation handling; it does not measure a future optimization that reuses a platform resolve. The 1× case isolates the single-sample copy more closely. Scissor is disabled for the full copy, READ/DRAW bindings and read buffer are restored, and Sokol state is invalidated. The probe mirrors the base's V3D pre-resolve flush guard for samples >1. This is a **cost experiment**, not the proposal's future shared end-pass resolve implementation or Pi correctness acceptance evidence.
- Metal: `MTLBlitCommandEncoder` in **the renderer's current command buffer**, after end-pass and before `presentDrawable` in `sg_commit`; private same-format texture. `GPUStartTime`/`GPUEndTime` from completion measure whole-command-buffer GPU time. Copy-only timing is unavailable (`nan`), not reported as zero.
- D3D11: use `resolve_view` when present, otherwise `render_view`, take its single-sample texture, and `CopyResource` into a same-format default-usage texture. Existing MSAA resolve stays in end-pass. Disjoint/timestamp queries measure scene and copy separately.

Default schedule: eight equal blocks **off/on/on/off/off/on/on/off**, 60 warmup frames then 180 measured frames per block, i.e. 720 samples per mode per window. Textures are allocated on the first on block (during excluded warmup) and reused; subsequent off blocks retain the allocation but issue no copy. Thus this measures steady-state copy execution, not reclaiming memory on disable. Animation varies across blocks; use block ranges/repeats to assess scene/noise effects.

GL/GLES3 use asynchronous elapsed-time queries (GLES requires `EXT_disjoint_timer_query`), with separate scene and copy intervals. Whole GPU time is their sum; it excludes presentation/compositor time and CPU gaps between queries. D3D11 timestamps include the gap between scene-end and copy-end. Metal measures the submitted command buffer as a whole, so backend timing definitions are not identical. Timer instrumentation can perturb batching and is present in both modes; no timing-based pass/fail threshold is used. Queries/results are collected without waiting in measured frames; shutdown drains only after all blocks. Disjoint samples are discarded. Per-block P95 is in `raw.log`; `summary.json` contains sample-weighted means, block ranges and off/on deltas. GPU time is not CPU time on llvmpipe: its “GPU” API reports a software renderer executing on host CPUs.

`cpu_submit_ms` spans entry to `App::draw` through `afterFrame` (includes command encoding and `sg_commit`, excludes GL/D3D native swap after callback return). `frame_interval_ms`/FPS includes event-loop pacing and swap waits between draw calls. **A requested `--vsync 0` is not a promise of uncapped operation at this base:** native desktop GL and D3D11 coerce zero to one; Metal's display-link pacing also applies. Use GPU deltas to expose cost hidden by a 60 Hz cap. Setting `--vsync 1` makes the intended capped run explicit. No production pacing code is changed.

### Local results

Measured here: **Ubuntu 24.04.4, Intel Core i5-12400, Mesa 25.2.8 llvmpipe (LLVM 20.1.2, 256 bits), Xvfb 4096×2304×24**, no accessible hardware GPU. GL context: 4.5 core; GLES context: ES 3.2. All test framebuffers logged **1920×1080**. Each row has 240 CPU and 240 valid GPU-query samples per mode. The final matrix completed all five jobs successfully. GL retained RGBA8 (8,294,400 payload bytes/window); GLES inspected alpha=0 and retained RGB8 (6,220,800 logical payload bytes/window, physical allocation not measured).

Values below are sample-weighted means from the final source version. GPU columns are **software-renderer API timer intervals**, not native device timings. “Copy” for 4× includes the probe's snapshot resolve. FPS is delivered callbacks/s under this Xvfb run; requested vsync does not make Xvfb a physical-display refresh/compositor model.

| Configuration | Window | GPU off → on (ms) | GPU delta (ms) | On: copy/resolve interval (ms) | FPS off → on | FPS delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GL, 1 window, 1× | 0 | 9.030 → 25.875 | +16.844 | 14.631 | 55.83 → 29.21 | -47.68% |
| GL, 2 windows, 1× | 0 | 9.393 → 21.431 | +12.038 | 11.871 | 49.09 → 26.97 | -45.06% |
| GL, 2 windows, 1× | 1 | 11.678 → 24.829 | +13.151 | 13.021 | 18.43 → 11.70 | -36.54% |
| GL, 1 window, 4× | 0 | 8.423 → 18.616 | +10.193 | 11.381 | 54.90 → 38.05 | -30.69% |
| GLES3, 1 window, 1× | 0 | 7.289 → 15.324 | +8.034 | 9.678 | 45.22 → 34.75 | -23.15% |
| GLES3, 1 window, 4× | 0 | 15.435 → 34.065 | +18.630 | 17.006 | 22.76 → 16.82 | -26.08% |

| Configuration | Wall frame interval off → on (ms) | CPU draw-to-afterFrame off → on (ms) |
| --- | ---: | ---: |
| GL, 1 window, 1×, window 0 | 17.911 → 34.233 | 1.118 → 16.535 |
| GL, 2 windows, 1×, window 0 | 20.371 → 37.081 | 0.823 → 13.560 |
| GL, 2 windows, 1×, window 1 | 54.260 → 85.506 | 0.776 → 13.946 |
| GL, 1 window, 4×, window 0 | 18.215 → 26.279 | 1.272 → 12.749 |
| GLES3, 1 window, 1×, window 0 | 22.115 → 28.777 | 1.798 → 11.600 |
| GLES3, 1 window, 4×, window 0 | 43.945 → 59.453 | 1.836 → 25.066 |

The two-window barrier keeps both owners in the same copy mode. They still have different native callback rates: main-window GLX swap and secondary-window timer pacing share a busy render thread. Report them separately; this is not a controlled pair of independent 60 fps displays. The local windows use default positions and may overlap. The block ranges/P95s in the raw records show variation from animation, scheduling and the shared host. These are single-run observations, not confidence intervals or a throughput prediction for Pi, Air or Windows.

The copy visibly reduces delivered FPS in every measured configuration here. The new resolve path's **format validation is also essential**: the original GLES 4× RGBA8-destination attempt failed and is excluded from the table. The source-format correction is confined to the probe; production still needs its own approved compatible resolve path and native V3D correctness evidence.

Evidence: [renderer info](tools/probes/snapshot-cost/results/local-glxinfo.txt), [matrix statuses](tools/probes/snapshot-cost/results/local-matrix.log), [GL 1-window raw](tools/probes/snapshot-cost/results/linux-gl-1080-1w-s1/raw.log), [GL 2-window raw](tools/probes/snapshot-cost/results/linux-gl-1080-2w-s1/raw.log), [GL 4× raw](tools/probes/snapshot-cost/results/linux-gl-1080-1w-s4/raw.log), [GLES 1× raw](tools/probes/snapshot-cost/results/linux-gles-1080-1w-s1/raw.log), [GLES 4× raw](tools/probes/snapshot-cost/results/linux-gles-1080-1w-s4/raw.log). Each directory also contains the full summary JSON and source-hash/command/environment metadata. The failed GLES attempt and diagnostic are retained as `results/gles-s4-rgba8-failure.log` and `results/gles-s4-format-diagnostic.log`; they are not cost measurements.

### Routed hardware status

| Requested device/configuration | Measurement status |
| --- | --- |
| Native Pi, 1920×1080, one window | Not available in this session; route the commands below. Desktop software GLES is not V3D evidence. |
| Native Pi, 1920×1080, two windows | GLES3 at this base is explicitly single-window. The GLES `--windows 2` command exits 2 with an explanation. Use the optional GLCORE/GLX Pi build for an actual two-window test if the driver supports it; no GLES two-window figure can be claimed. |
| Air Metal, Retina | Not run here; route commands below and use logged physical size. |
| Windows D3D11, 3840×2160 | Not run here; route commands below and verify logged physical size. |

Do not substitute a memory/bandwidth estimate or local llvmpipe number for any missing native measurement. This report supplies reproducible jobs; hardware cost approval remains pending those results.

## Exact build/run commands

Run from the clone root at the specified base, with the untracked probe directory copied to that clone. Use separate build directories for the two Linux backends. Requires the normal base TrussC platform development dependencies, CMake, a compiler, Python 3, and native desktop access. Generated shader headers/tool downloads follow the core's ordinary build. Require the logged physical framebuffer extent to match the requested test size on every window; reject silently resized runs. Keep the window visible, avoid moving/resizing it during a run, finish compilation before measuring, and record power/thermal settings alongside each result. `run.py` writes `metadata.json` (including probe source SHA-256 hashes), `raw.log`, and `summary.json`; nonzero app status fails the job. Its output directory is relative to the clone root.

### Pi: native GLES3, 1080p, one window; verify two-window limitation

On Pi OS in the desktop session, use the existing DISPLAY (X11 or an XWayland setup already usable by TrussC). Do not set `LIBGL_ALWAYS_SOFTWARE`.

```sh
git rev-parse HEAD
cmake -S tools/probes/snapshot-cost -B /tmp/snapshot-cost-pi-gles -DCMAKE_BUILD_TYPE=Release -DPROBE_GLES3=ON
cmake --build /tmp/snapshot-cost-pi-gles --parallel 2
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gles/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gles-1080-1w-s1 --width 1920 --height 1080 --windows 1 --samples 1 --vsync 1
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gles/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gles-1080-1w-s4 --width 1920 --height 1080 --windows 1 --samples 4 --vsync 1
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gles/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gles-1080-2w-unsupported --width 1920 --height 1080 --windows 2 --samples 1 --vsync 1
```

The last command is an expected capability failure, **not a benchmark**. Source: `core/include/tc/app/tcWindow.h:561`–590 and `core/platform/linux/tcWindowLinux.cpp:19`, which compile the real secondary-window adapter only for `SOKOL_GLCORE`. Do not add production GLES multi-window support in this measurement task.

For Pi with a working desktop GL/GLX driver that supports the base's default **OpenGL 4.3 core** context (`sokol_app_tc.h:10600`), obtain an actual one/two-window comparison using the same backend for both. This forces desktop GL only in the probe build; retain its distinct GL/V3D labels in the report.

```sh
cmake -S tools/probes/snapshot-cost -B /tmp/snapshot-cost-pi-gl -DCMAKE_BUILD_TYPE=Release -DPROBE_GLCORE=ON
cmake --build /tmp/snapshot-cost-pi-gl --parallel 2
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gl/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gl-1080-1w-s1 --width 1920 --height 1080 --windows 1 --samples 1 --vsync 1
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gl/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gl-1080-2w-s1 --width 1920 --height 1080 --windows 2 --samples 1 --vsync 1
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gl/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gl-1080-1w-s4 --width 1920 --height 1080 --windows 1 --samples 4 --vsync 1
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-pi-gl/bin/snapshotCost --out tools/probes/snapshot-cost/results/pi-gl-1080-2w-s4 --width 1920 --height 1080 --windows 2 --samples 4 --vsync 1
```

If native GL creation fails, retain its error; a two-process GLES run is not equivalent to two windows sharing this renderer and cannot fill the two-window cell. In a real two-window run report **each window's FPS/GPU time**, not an FPS sum represented as a single-window rate.

### Air: native Metal / Retina

```sh
git rev-parse HEAD
cmake -S tools/probes/snapshot-cost -B /tmp/snapshot-cost-air -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/snapshot-cost-air --parallel 4
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-air/bin/snapshotCost --out tools/probes/snapshot-cost/results/air-retina-s1 --width 2560 --height 1600 --windows 1 --samples 1 --vsync 1
python3 tools/probes/snapshot-cost/run.py --exe /tmp/snapshot-cost-air/bin/snapshotCost --out tools/probes/snapshot-cost/results/air-retina-s4 --width 2560 --height 1600 --windows 1 --samples 4 --vsync 1
```

The main app enables high-DPI and `pixelPerfect`, so width/height request **physical framebuffer pixels**: `buildAppDescriptor` divides by the detected display scale before native window creation (`core/include/TrussC.h:3259`). At scale 2, a 2560×1600 request creates a 1280×800 point content area with a 2560×1600 backing image. An alternative 2940×1912 request uses `--width 2940 --height 1912`. Actual backing size may differ with the display mode/window manager: accept the run only at the desired logged `WINDOW ... physical=...`/`SNAPSHOT ... physical=...`, and use that physical size in the memory formula. The base's main Metal layer already has `framebufferOnly=NO`. These Metal probe branches are source-prepared, not compiled or run in this Linux session.

### Windows: D3D11, 4K

PowerShell in a **VS 2022 x64 developer environment**, with CMake and Python 3 available:

```powershell
git rev-parse HEAD
cmake -S tools/probes/snapshot-cost -B tools/probes/snapshot-cost/build-win -G "Visual Studio 17 2022" -A x64
cmake --build tools/probes/snapshot-cost/build-win --config Release --parallel 4
py -3 tools/probes/snapshot-cost/run.py --exe tools/probes/snapshot-cost/build-win/bin/Release/snapshotCost.exe --out tools/probes/snapshot-cost/results/windows-4k-s1 --width 3840 --height 2160 --windows 1 --samples 1 --vsync 1
py -3 tools/probes/snapshot-cost/run.py --exe tools/probes/snapshot-cost/build-win/bin/Release/snapshotCost.exe --out tools/probes/snapshot-cost/results/windows-4k-s4 --width 3840 --height 2160 --windows 1 --samples 4 --vsync 1
```

Use a 4K display and verify **physical=3840×2160** in the log. The main app enables `pixelPerfect`, requesting physical pixels via the base display-scale conversion; startup DPI detection and the monitor on which the window lands must agree. A 100% scaling display avoids that startup ambiguity. Do not manually divide the command dimensions by DPI unless the log shows the base failed to perform that conversion. Windows DPI/window-size behavior, decorations and work-area constraints can affect the resulting size. The D3D11 probe branch is source-prepared, not compiled or run here. GPU timings are timestamp-query intervals, not the CPU duration of `CopyResource`.

### Local Linux reproduction

```sh
cmake -S tools/probes/snapshot-cost -B /tmp/snapshot-cost-gl -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/snapshot-cost-gl --parallel 4
cmake -S tools/probes/snapshot-cost -B /tmp/snapshot-cost-gles -DCMAKE_BUILD_TYPE=Release -DPROBE_GLES3=ON
cmake --build /tmp/snapshot-cost-gles --parallel 4
env __GLX_VENDOR_LIBRARY_NAME=mesa __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/snapshot-cost-mesa-cache xvfb-run -a -e /tmp/snapshot-cost-matrix-final-xvfb.log -s '-screen 0 4096x2304x24 -nolisten tcp -noreset' python3 tools/probes/snapshot-cost/local-matrix.py
```

The local matrix uses 20 warmup + 60 measured frames in each of eight blocks (240 measured frames/mode/window), runs configurations sequentially, and saves GLX renderer info. Builds were finished before measured runs. The explicit Mesa vendor environment is needed here because Xvfb otherwise crashes while initializing the installed NVIDIA EGL vendor; `-noreset` avoids a reset race between the GLX preflight client and the app. These overrides select **software rendering for local reproducibility only**; do not copy them into the native Pi/Air/Windows commands.

## Contract facts before comparing policies

The base API comment (`core/include/TrussC.h:2176`) permits immediate `grabScreen` in update/afterFrame but warns that deferred draw work is not yet rendered during draw. It does not define an immutable completed-frame serial. The proposal strengthens this to “latest completed frame” in any supported phase, including a nested foreign update. Both that strengthened rule and any first-use restriction require owner approval; unchanged C++ signatures do not mean unchanged timing semantics.

`tc::present()` and native presentation are distinct:

| Backend | When the base actually presents | Consequence |
| --- | --- | --- |
| GLCORE | `sokol_app_tc.h:10943`/10988 swap **after** the window frame/tick callback returns | `afterFrame` is after `tc::present()` but before native swap; that window's completed back buffer is readable there. |
| GLES3 | `sokol_app_tc.h:15177`–15186 does the frame callback then `eglSwapBuffers` | Same useful pre-swap `afterFrame` boundary. |
| D3D11 | `sokol_app_tc.h:7230`/7279 calls native `Present` after the callback | `afterFrame` can copy/map the completed back buffer before native Present. |
| Metal | `sokol_gfx.h:16390`–16400 schedules every pending drawable and commits inside `sg_commit` | `afterFrame` is already past scheduling/submission of present. Capture must have encoded a transfer **before** that commit. |

Reading before native presentation is supported on all these APIs when the correct ended/resolved source is still valid. It is not a general after-native-presentation guarantee. Apple's readback example encodes its blit before presenting because drawable contents are discarded; it also disables framebuffer-only restrictions. [Apple: reading drawable pixels](https://developer.apple.com/documentation/metal/reading-pixel-data-from-a-drawable-texture), [CAMetalLayer.framebufferOnly](https://developer.apple.com/documentation/quartzcore/cametallayer/framebufferonly).

The D3D11 base uses **FLIP_DISCARD** (`sokol_app_tc.h:6907`), so it cannot promise last-presented contents survive. GLX likewise does not promise usable new back-buffer contents after swap; EGL preservation is surface-dependent and requesting it can itself cost performance. Neither a COM retain nor a strong Metal drawable retain freezes image contents. [Microsoft: swap effects](https://learn.microsoft.com/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect), [Khronos: EGL preservation](https://registry.khronos.org/EGL/specs/EGLTechNote0001.html).

The initial GLES 4× attempt with an RGBA8 retained image failed with `GL_INVALID_OPERATION` (0x502). A diagnostic wrapper observed source RGB=8/8/8, alpha=0 versus destination 8/8/8/8, with no preceding GL error. This matches the GLES multisample-blit requirement for identical source/destination formats. The final probe inspects alpha bits and uses RGB8 for that source; capture output can still normalize to RGBA8 later. The failed attempt/diagnostic are retained separately and are not timing evidence. [Khronos: GLES 3.1 specification, framebuffer blits](https://registry.khronos.org/OpenGL/specs/es/3.1/es_spec_3.1.pdf).

A “capture from another window's update” means dispatch to an explicitly selected target; ordinary `grabScreen(Pixels&)` still selects the **calling window**. Its signature does not take an arbitrary window handle. The foreign case arises through explicit window dispatch/nesting or internal MCP targeting. Restore the target's actual GL context before native reads and restore the caller afterwards; never interpret framebuffer 0 as an identity shared across windows.

## Four alternatives

The following describes proposed behavior, not code implemented by this task. “No completed frame” includes setup before the first render, lost/failed snapshot generation, and an extent/format mismatch. Do not manufacture a fallback drawable or return an older serial as the newly completed frame.

### (a) Always snapshot

**Rule:** Every native window copies every finalized frame to its retained native-format texture before presentation, whether or not anyone captures.

**Backends:** Metal encodes a resolved-drawable-to-texture blit in the owner renderer buffer; GL/GLES use the guarded final resolve/copy before swap; D3D11 copies its already resolved back buffer before Present. All then read their retained source. An app that never captures pays the full image allocation and one copy per rendered frame per window (format-dependent tables above), plus any extra tile-store/resolve cost. CPU readback is still capture-only.

**Consumers:** `grabScreen` synchronously stages the selected latest completed serial and returns it; before the first completed frame it returns false. `saveScreenshot` keeps queued success/path semantics and at the owner's afterFrame reads that completed frame. `ScreenRecorder.start` binds the source owner/extent; its first eligible afterFrame reads the finalized frame and subsequent accepted frames use the same source policy. MCP screenshot/get/save/analyze stages execute through their existing target scheduler and capture the completed frame, then CPU-encode elsewhere. No extra MCP request queue is introduced.

**Edges:** After native present and during another window's update, the owned image remains usable. During a new frame, return the previous completed serial, not its unfinished prefix. Async issue must encode image→job-staging **before** any later overwrite on the same queue; retaining the reusable snapshot handle alone is insufficient. Resize/close pins old generations only for issued jobs. Minimized/no-draw windows keep their last valid source without making up a new frame. Owner-authorized presentation still must suppress unfinished foreign drawables during prefix submission, independently of snapshot policy.

### (b) Arm each window on first real capture use

**Rule:** A window starts unarmed with no retained image; its first valid capture use arms future finalized frames, after which it behaves like (a) for that window's lifetime.

**Backends:** All three native backend families use the same lazy snapshot policy; Metal still must copy before present, GL/GLES and D3D11 can optionally serve a safe pre-swap first call directly. An app that never captures pays **zero snapshot bytes and zero snapshot copies**. Once armed it pays (a)'s recurring cost, even after a one-off screenshot or recorder stop. Registering MCP tools, constructing a recorder, or naming a window must not arm anything: only an actual accepted use does. Failed admission/start should not impose permanent cost.

**First `grabScreen`:** A strictly uniform implementation **arms and returns false** until a later owner-finalized frame produces its first snapshot. It must not block the render thread waiting for the next frame it is responsible for rendering, return blank current pixels, or quietly return some older image. A no-draw/event-driven owner needs a redraw request to produce that source; for an occluded owner the miss can persist. C++ signature stays synchronous, but this cold miss changes the proposal's first-call contract even if the window has rendered thousands of earlier frames.

**Can a one-off prefix-submit avoid that change?** Only in a limited phase. If the selected owner has already finalized its current frame **and native presentation has not yet been authorized**, it can capture that exact completed frame directly. On Metal: end/park encoders, encode the drawable→staging transfer in the current renderer prefix before any present, submit without presentation, then wait synchronously; alternatively submit the earlier prefix first and order a staging buffer on the same queue before present. On GL/GLES: end/resolve and read the selected back buffer before swap (use a temporary resolve destination for GLES multisample readback). On D3D11: copy the resolved back buffer to staging before Present and Map. This is capture-only cost and does not require a permanent snapshot just for that read.

That exception **does not** solve a cold request in the next update, after native presentation, or while the current frame only has an unfinished prefix: the requested previous completed image was never retained. Copying the unfinished prefix changes which image is returned; finalizing/presenting the window just to answer capture changes rendering order. Metal at existing `afterFrame` is already too late. No contract-preserving universal first-call prefix path has been established.

**Other consumers:** `saveScreenshot` arms at valid enqueue, before owner finalization; its already deferred capture can return the next finalized requested frame with normal queued/file-failure distinction. `ScreenRecorder.start` arms after the writer/source is validated and before the next frame's finalization; the first accepted frame is that next owner completion, never stale pre-start pixels. MCP arms at existing **admission/target scheduling before finalization**, then runs its ordinary deferred stage on the new completed source; arming for the first time inside its current afterFrame `grabScreen` is too late on Metal and would fail the first request. A request admitted after finalization waits for the next owner frame under the existing scheduler/timeouts. This must be integrated with #863's scheduler owner, not implemented as another queue.

**Edges:** Once warm, after-present, foreign-update and async behavior match (a). Cold async may fail immediately or, only with an explicitly approved “next frame” contract, stay in the consumer's existing deferred request until a completed source exists; those are different serial semantics and must be documented. Cold synchronous foreign capture has no permission to render/reenter/finalize the other window. Start during an already completed afterFrame captures from the next owner completion. Resize can temporarily make the source unavailable until a new-sized frame completes. Stopping a recorder does **not** disarm literal (b).

### (c) Explicit opt-in setting — recommended simplest policy

**Rule:** Each window chooses completed-frame retention at creation (default off); enabled windows snapshot like (a), and disabled windows explicitly reject screen-capture consumers that require a retained completed source.

**Backends:** Enabled: same per-backend production and readback as (a); disabled: no extra color texture and no snapshot blit/resolve on Metal, GL/GLES or D3D11. This setting changes the screen-capture contract, not ordinary rendering, Fbo reads or presentation authority. Make presentation readiness independent of “snapshot encoded” when disabled, and independent of capture allocation failure when enabled. A never-capturing app with the default setting pays **zero extra snapshot bytes/copies**; a never-capturing app that deliberately enables it pays (a)'s cost.

**Consumers:** When enabled before frame 1, `grabScreen` returns the latest retained completed frame in all supported phases (false before frame 1), `saveScreenshot` queues and drains normally, `ScreenRecorder.start` binds and begins at the next eligible completed frame, and MCP screenshot follows its existing admitted target schedule. When disabled, `grabScreen`/`captureWindow` return false; `saveScreenshot` rejects before enqueue/path side effects where possible; screen `ScreenRecorder.start` returns a capture-not-enabled `SaveResult` before opening a writer; MCP screenshot returns a clear per-window unsupported/not-enabled error rather than timing out or silently enabling perpetual copies. Fbo-source recording/capture continues to work without this setting. Web retains its existing capability split.

**Edges:** Enabled after-present/foreign-update/async behavior matches (a). Recommend creation-time choice for PR 5 simplicity. If a runtime toggle is later allowed, enabling cannot reconstruct earlier frames: invalidate, request redraw and return false until a new generation completes; disabling stops future copies but retains only resources already owned by issued jobs. Do not resize/close a backbuffer while jobs retain swapchain references; jobs own independent staging. ScreenRecorder start on an unenabled owner fails visibly rather than retroactively changing that owner's frame budget. This is explicit user configuration and requires approval as a behavior change; it cannot be advertised as preserving all existing no-setting capture applications unchanged.

### (d) Backend split: direct pre-swap capture on GL/D3D11; snapshots on Metal

**Rule:** GL/GLES and D3D11 read only a valid ended/resolved back buffer before native swap/Present, while Metal uses a retained completed image produced before its commit.

**Backends and never-capture cost:** GL/GLES and D3D11 can have **zero idle snapshot bytes/copies**; a direct capture still incurs readback/staging and, where necessary, a one-off resolve. If Metal snapshots unconditionally, it still pays (a) even when never capturing. Making Metal lazy/opt-in is an additional (b)/(c) policy; it must be stated and has their cold/disabled behavior.

**Consumers:** GL/D3D11 `saveScreenshot`, recorder accepted frames, and existing MCP deferred screenshot stages can capture at the actual pre-swap afterFrame boundary and own CPU pixels/staging until encoding/delivery; no retained full-frame image is needed on uncaptured frames. `ScreenRecorder.start` just registers for the next eligible owner boundary. Metal must register pending work before finalization, or retain every frame; an afterFrame-only decision is late. GL/D3D11 `grabScreen` succeeds only if the desired completed back buffer still exists in that valid pre-present interval. In a later update after swap, return false rather than pretend that the new back buffer is the last displayed frame. Metal `grabScreen` follows whichever always/lazy/opt-in snapshot policy is chosen.

**Edges:** After **`tc::present()` but before native present**, GL/D3D11 direct capture works; after **native present**, it has no guaranteed source. A foreign-window update may run after that target's native swap or while it is unfinished, so direct capture cannot guarantee that target's previous completed serial. Async GL work must issue its owning-context PBO/resolve transfer before swap and map later with correct ownership; D3D11 must issue the source→job-staging copy before Present. Both can defer CPU delivery, not the source read itself. These are async transfer requirements if such paths are used, not a claim that PR 5 should add a new public GL async capture API; existing GL capture can instead produce owned CPU Pixels synchronously before worker encoding. Do not re-render or force a foreign swap as a hidden repair. A backend split therefore supports scheduled capture efficiently, but **does not implement the proposal's uniform immediate contract** merely because its APIs can read before swap.

## Comparison for the owner

| Policy | Never captures, default use | First ad-hoc synchronous capture after native presentation | Scheduled screenshot / recorder start | Main tradeoff |
| --- | --- | --- | --- | --- |
| (a) Always | Image + copy every rendered frame, every backend | Latest completed frame available (if successfully produced) | Works from completed source | Invisible permanent frame-budget/memory tax |
| (b) First-use arm | Zero snapshot work until actual first use | Arms, returns false until next completion | Works if arming occurs before finalization | Cold miss; one convenience request can create permanent tax |
| (c) Explicit opt-in, default off | Zero snapshot work unless intentionally enabled | Disabled: explicit failure; enabled from creation: latest completed | Disabled: fail clearly; enabled: works | Requires configuration; most predictable cost |
| (d) Split | GL/D3D11 zero; always-snapshot Metal still pays | GL/D3D11 no guaranteed source; Metal depends on retention policy | Direct pre-present GL/D3D11; Metal needs early registration/retention | More phase/backend rules, cannot offer uniform first-call guarantee |

Whichever policy is approved, the pending PR's owner-local recording, non-presenting prefix submission, foreign-presentation suppression, source identity/extent/format, checked layout, async staging-before-overwrite, and job lifetime rules remain necessary. Snapshot convenience must not become permission to present another owner's unfinished drawable, rewind foreign CPU records, stall every normal frame, or withhold ordinary presentation when capture is disabled. This task implements none of those production changes.
