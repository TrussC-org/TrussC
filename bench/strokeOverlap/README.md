# Stroke overlap benchmark (#1386)

Research only: this standalone project makes no core API decision and reserves no stencil bits. It needs no addons or trusscli generation. Use the same checkout, compiler, driver and machine when comparing runs.

The deterministic scenes are 200 long crossing strokes (24 points each), 5,000 short strokes (4 points each), and one self-crossing stroke (2,000 points). Seed: `0x1386`, a specified 32-bit LCG; alpha 0.5; four cycling colors; round joins and caps. Width is 12 px at 480 high and 27 px at 1080 high. Core `StrokeMesh` builds the triangles once, outside timing. All passes use RGBA8/depth-stencil Fbos, sample count 1.

| Mode | Operation |
| --- | --- |
| none | Plain alpha blending; all strokes batch into one draw; self-overlap accumulates opacity (the benchmark uses a black background). |
| stencil_clear | NotEqual 1, Replace 1, color on; full-target `clearStencil()` after each stroke. |
| stencil_cycle | NotEqual v, Replace v; v = (stroke index modulo 255) + 1; clear before reusing values. All 255 reference pipelines are created before timing. |
| stencil_redraw | NotEqual 1, Replace 1, color on; redraw the identical triangles with Always/Replace 0 and color writes off. |
| union_frame | Clipper2 round offset and earcut per stroke inside frame timing, then plain blending. |
| union_cached | Offset and triangulate once outside timing; draw cached triangles with plain blending. |

Different strokes still blend in order. One bench-local submitter assembles a single vertex stream and ordered draw/clear commands each frame. Adjacent triangles batch until the pipeline changes or a clear is needed. It uploads once with `sg_write_buffer_transient`, then issues the commands using the internal target pipeline API. Its shader source is `src/triangles.glsl`; the pinned shader compiler generates the backend shaders at build time. Clear operations use the existing core full-target clear path. Clear counts include only explicit per-mode clears, excluding the common initial clear. Pipeline counts include the initial bind and explicit clears; they exclude the common setup and final drawing-state restoration. Draw counts include stencil-clear triangles.

The independent reference renders one stroke's identical triangles opaque white into a transparent scratch coverage Fbo cleared for every stroke, then composites the coverage onto a black opaque accumulated image using that stroke's color at alpha 0.5. The premultiplied Fbo tint includes opacity in RGB. This happens once per scene/resolution, outside timing, one stroke per host frame to avoid transient Fbo version accumulation. Stencil is disabled throughout reference construction.

Each case uses 30 warmup frames, then up to 300 measured frames. A roughly 60-second budget including warmup can shorten a case; the table/JSON records the actual measured count. CPU covers vertex assembly, any offset/triangulation, upload, draw submission and Fbo end. JSON reports median Clipper2 and earcut times separately; draw/submit is the remaining CPU time (including assembly, allocation cleanup and Fbo end). Component medians need not sum to the CPU median. Cached preprocessing is reported once per scene/resolution, including a Clipper2/earcut split, and excluded from all frame timing. Wall also includes completion: `glFinish` on Linux GL; full color readback elsewhere (additional readback cost on D3D11). Common target setup and initial color/stencil clear are excluded from CPU timing; pre-frame GL completion also excludes them from Linux wall timing. On other backends that initial GPU work can still be queued when timing starts. Median and p95 use linear interpolation of sorted samples. No GPU timer is reported. Shared-machine scheduling and fixed case order affect these informational samples. **llvmpipe is a CPU rasterizer, so its fill costs are exaggerated.**

Readbacks outside timing compare all RGBA channels to the reference: maximum byte difference, mean absolute byte difference, and pixels with any channel more than 2/255 off. Stencil modes fail the run if any pixel exceeds that threshold. Mode none is expected to differ. `--images` saves the reference and all available mode images for 640x480, plus a 128x128 center crop (none left, reference right). The default matrix runs both resolutions; `--resolution 480` or `--resolution 1080` selects one. `--output` selects the JSON path (default: results.json beside the executable). Table output includes backend/renderer; JSON includes the same metadata. Logs/results/images belong outside the bench source directory.

## Linux (x86_64, local dependencies)

Run from the repository root. The system compiler, GL/X11 development libraries and Xvfb must already be installed. Stop if the local pinned shader compiler is missing or has a different hash; do not allow a shader compiler download. The commands use the worker's build wrapper; no FetchContent downloads are permitted.

```sh
TC="$PWD"
P="$TC/bench/strokeOverlap"
B=/home/toru/TrussC-dev/trussc-ai-tools/tcbuild.sh
CLIPPER2=/home/toru/TrussC-dev/build-cache/deps/clipper2-1.5.4
test -x "$TC/core/tools/sokol-shdc/sokol-shdc" || exit 1
test "$(sha256sum "$TC/core/tools/sokol-shdc/sokol-shdc" | cut -d ' ' -f 1)" = ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df || exit 1
$B cmake -S "$P" -B "$TC/.build/strokeOverlap" -DTRUSSC_DIR="$TC/core" -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DSTROKE_OVERLAP_CLIPPER2=ON -DCLIPPER2_SOURCE_DIR="$CLIPPER2"
$B cmake --build "$TC/.build/strokeOverlap" --parallel 4
mkdir -p "$TC/.verification"
export LIBGL_ALWAYS_SOFTWARE=1
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
Xvfb :94764 -screen 0 1280x800x24 > "$TC/.verification/xvfb.log" 2>&1 & XP=$!
trap 'kill "$XP"' EXIT
for attempt in $(seq 1 50); do DISPLAY=:94764 xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
DISPLAY=:94764 "$P/bin/strokeOverlap" --output "$TC/.verification/results.json" --images "$TC/.verification" > "$TC/.verification/run.log" 2>&1
# The command above runs the entire 640x480 and 1920x1080 matrix once.
# Alternatively run either half (same binary, same Xvfb):
# DISPLAY=:94764 "$P/bin/strokeOverlap" --resolution 480 --output "$TC/.verification/results-480.json" --images "$TC/.verification"
# DISPLAY=:94764 "$P/bin/strokeOverlap" --resolution 1080 --output "$TC/.verification/results-1080.json"
kill "$XP"
trap - EXIT
```

## Windows (Developer PowerShell, Visual Studio)

Run from the repository root with Visual Studio 2022 C++ tools and Windows SDK installed. The default Windows backend is D3D11. A pinned Windows-native sokol-shdc must already be present; do not copy the Linux binary or download missing tools. Standard native CMake commands are shown for the Windows device where the Linux build wrapper is unavailable. No trusscli generation is used.

```powershell
$TC = (Get-Location).Path
$P = Join-Path $TC 'bench/strokeOverlap'
$Clipper2 = 'C:/deps/clipper2-1.5.4' # Local extracted pinned release; adjust this path
if (!(Test-Path "$TC/core/tools/sokol-shdc/sokol-shdc.exe")) { throw 'Missing local sokol-shdc; stop' }
if ((Get-FileHash "$TC/core/tools/sokol-shdc/sokol-shdc.exe" -Algorithm SHA256).Hash -ne 'bd616287f9ea689d53c6d260e443ee733e61ae1b73a9b37adc482ead0364d561') { throw 'Wrong shader compiler; stop' }
cmake -S $P -B "$P/build-windows" -G 'Visual Studio 17 2022' -A x64 -DTRUSSC_DIR="$TC/core" -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DSTROKE_OVERLAP_CLIPPER2=ON -DCLIPPER2_SOURCE_DIR="$Clipper2"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build "$P/build-windows" --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
New-Item -ItemType Directory -Force "$TC/.verification" | Out-Null
& "$P/bin/strokeOverlap.exe" --output "$TC/.verification/windows-results.json" --images "$TC/.verification" *> "$TC/.verification/windows-run.log"
if ($LASTEXITCODE -ne 0) { throw 'Benchmark failed' }
# The command above runs both resolutions. To run the halves separately:
# & "$P/bin/strokeOverlap.exe" --resolution 480 --output "$TC/.verification/windows-480.json" --images "$TC/.verification"
# & "$P/bin/strokeOverlap.exe" --resolution 1080 --output "$TC/.verification/windows-1080.json"
```

The renderer field queries the DXGI adapter description on D3D11. Record the OS, CPU, GPU, driver, power mode and display configuration alongside results. Linux GL completion and D3D11 readback completion have different overhead and should not be treated as interchangeable GPU timings.

## Local Clipper2 dependency

Modes 5/6 require `-DSTROKE_OVERLAP_CLIPPER2=ON -DCLIPPER2_SOURCE_DIR="<path to clipper2-1.5.4>"`. The option defaults OFF; OFF builds only modes 1–4 and reports 5/6 unavailable. CMake compiles only `CPP/Clipper2Lib/src/*.cpp` into a small static library in the build directory, directly from the read-only local source. No Clipper2 tests, examples or utilities are configured. No dependency is copied, vendored or downloaded by this project.

Use the same pinned release on Windows and Linux:

- Tag: `Clipper2_1.5.4`; commit: `ef88ee97c0e759792e43a2b2d8072def6c9244e8`.
- Archive: https://github.com/AngusJohnson/Clipper2/archive/refs/tags/Clipper2_1.5.4.tar.gz
- Archive SHA256: `9d8a35a29d04cd1b7b45f542c0ba48015feece1210036ea9e4efaad3140af4b0`.

The local directory must contain `CPP/Clipper2Lib/include/clipper2/clipper.h` and `CPP/Clipper2Lib/src/*.cpp`. Obtain/extract that exact archive separately on the Windows machine and verify its SHA256 before configuring. These worker commands use only the already provisioned Linux source.

The adapter rounds coordinates onto a 0.001 px integer grid, uses `ClipperOffset` with `JoinType::Round`, `EndType::Round`, delta = width/2 and fixed arc tolerance **0.01 px** at both resolutions. This is close to core's cap tessellation (semicircle sagitta about 0.01285 px at width 12, 0.00571 px at width 27); core's join subdivision depends on angle and width, so the meshes are not identical. It offsets the original open polyline, without unioning the core triangles. Clipper2 returns a `PolyTree64`; each outer ring and its direct hole children form one polygon for the core-vendored `mapbox::earcut<uint32_t>`. Islands inside holes are processed recursively as separate outer rings. Each stroke's output is appended in order through the shared submitter with plain blending and no stencil; all strokes batch into one draw.

Pixel diffs for these modes include edge differences from offset geometry, coordinate rounding and tessellation. They use the same independent core-triangle reference and metric as modes 1–4, but do not enforce the stencil modes' 2/255 threshold. Frame/cached images must match each other. Join and self-crossing comparison crops (reference left, mode 5 right) are retained in `.verification/` with crop coordinates recorded there.
