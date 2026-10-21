# WebGPU uniform growth regression

This owned standalone test compiles its own Sokol implementation. It uses
`sg_desc.uniform_buffer_size` directly as a test seam, without a public API
addition. Build Release so the original unchecked path is exercised.

Run from the repository root with emsdk and Chrome installed:

```sh
python3 core/tests/webgpuUniformGrowth/run.py --build
```

`TC_BUILD_WRAPPER` selects the local build guard. On the shared Linux builder
it defaults to the installed `tcbuild.sh`. The script serves the build on an
ephemeral localhost port, prints the URL, launches headless Chrome with
SwiftShader WebGPU, and captures the console and completion verdict:

```text
WEBGPU_UNIFORM_GROWTH: PASS (6 frames, 3 raster/compute passes, tiny/large pixels, allocation rollback, async GPU failure)
```

The six frames compare every pixel against both analytic per-tile RGBA values
and the same scene with a 4 MiB reserve. The 256-byte reserve forces several
mid-frame growths. Each draw uploads distinct vertex and fragment blocks with
reversed WebGPU binding order. An odd compute-upload prefix makes growth fall
between the two stages of a draw. Three interleaved raster/compute passes per
frame test pass transitions and storage-buffer compute readback. Later frames
reuse the grown buffer. Fault injection covers CPU allocation, GPU buffer and
uniform/resource bind-group creation, null cache allocation, failed slot
cleanup and next-frame recovery. A failed upload also attempts raster and
compute encoding to verify both are skipped. In one frame a growth requests an
invalid buffer usage, so Chrome returns a non-null error object and reports the
validation error later through the backend's error scope. That frame keeps
uploading, binding and drawing with the error object and is submitted. The
next frame must return to the previous buffer with one Error, render every tile
correctly, and the run must show no uncaptured WebGPU error. The validation
error from using the error object is captured and logged through sokol with its
message (`WebGPU error captured while new uniform resources were unconfirmed`);
that line is expected in this step.

The timeouts in `run.py` are hang guards; no pass criterion depends on elapsed
time. Chrome background networking is disabled and DNS is restricted to
localhost. `TC_WEBGPU_CDP_TRACE=1` prints DevTools and HTTP progress for diagnosing
sandbox failures. The script stops its own Chrome PID and local server.

For an interactive Chrome on macOS/Windows, configure/build the same CMake
project with emsdk, serve its output, and open the printed address:

```sh
emcmake cmake -S core/tests/webgpuUniformGrowth -B core/tests/webgpuUniformGrowth/build-web -DCMAKE_BUILD_TYPE=Release
cmake --build core/tests/webgpuUniformGrowth/build-web
python3 -m http.server 8630 --directory core/tests/webgpuUniformGrowth/build-web
# Open http://localhost:8630/index.html
```

On the shared builder, prefix both build commands with the build guard.

The native mock uses the real emdawn WebGPU C declarations. On Linux and other
GNU-toolchain hosts, CMake finds them in the emsdk cache (`EM_CACHE`, `EMSDK`
or `~/emsdk`, present once emsdk has built a WebGPU target), so the default
native build, including `build_all.py --core-tests-only`, runs the mock. Pass
`WEBGPU_INCLUDE_DIR` to point at another emdawn `webgpu/include` directory.
The mock executes the actual backend helpers,
records 8192 draws/dispatches, models WebGPU reference retention, checks each
recorded block after submission, tests allocation/arithmetic rollback and
exercises 4999 cache keys with quiet eviction. It also models WebGPU error
objects: buffer and bind-group creation can return a non-null object whose
error scope reports OutOfMemory or Validation one frame later, and writing or
binding an error object raises a validation error (at the write, or at finish
and submit) that counts as uncaptured unless a scope is open. Frames run
through the backend's begin pass, end pass and commit. The next frame must
return to the last good buffer and size with one Error, must not retry growth
while demand fits, must not reuse a uniform group built from the error object,
and a late callback after teardown must not affect a new backend. It also
covers a valid growth replacing an errored one before the results arrive (one
Error, valid buffer kept), a shader without uniform blocks whose empty group
fails while no frame uploads uniforms, and an unrelated error inside a
contained frame (logged once, not hidden, nothing rolled back). It also covers
an initial buffer that fails with no predecessor (shaders without uniform
blocks keep drawing; the next group or upload that needs a buffer recreates it
at the initial reservation), an app error in a frame where an unused group
fails (still logged), and containment results arriving before creation results
(the same errors are logged in either order). Every error a containment scope
captures is logged, so the failed frame's validation message appears next to
the one recovery Error.
Build with ASan/UBSan to check CPU memory and temporary-resource cleanup.
Without the headers (macOS, Windows, or no emsdk) the native executable only
prints `SKIP ...` and exits 0, which the batch runner still counts as passed;
treat that as "not run".

For a baseline comparison, set `SOKOL_INCLUDE_DIR` to the **base** tree's
`core/include/sokol` and `TC_WEBGPU_BASELINE=ON`, in a separate build directory.
The scene is identical; head-only failure seams are excluded. Under ASan the
base Release backend fails in `_sg_wgpu_apply_uniforms` at the first staging
write past its allocation. Do not treat an absent verdict, a browser validation
error or a skipped native executable as a passing browser test.

# Existing example screenshots

Use the same Chrome/adapter and viewport on base and head. Good static scenes
are `examples/graphics/colorExample`, `examples/graphics/roundedRectExample`
and `examples/3d/pbrSpheresExample`. Generate each example's CMake files with
`tools/bin/trusscli update -p <example> --tc-root . --ide cmake`, then configure
with `emcmake cmake -S <example> -B <example>/build-web -DCMAKE_BUILD_TYPE=Release
-DTC_WEB_BACKEND=WGPU` and build it. Prefix all build commands with the build
guard on the shared builder. Serve each example's `bin` with
`python3 -m http.server 8631 --directory <example>/bin`.

For each tree and scene, use its HTML filename and capture a fixed frame:

```sh
python3 core/tests/webgpuUniformGrowth/run.py --url http://localhost:8631/colorExample.html --frames 60 --screenshot base-color.png
# Repeat against head, using --screenshot head-color.png.
```

`--frames` fixes the clock/random seed and stops requestAnimationFrame at the
requested frame. Use a pixel comparison, not PNG file hashes (metadata may
differ). For example, with Pillow installed:

```python
from PIL import Image, ImageChops
base = Image.open('base-color.png').convert('RGBA')
head = Image.open('head-color.png').convert('RGBA')
assert base.size == head.size
pixels = ImageChops.difference(base, head)
pixels.save('diff-color.png')
assert all(channel.getextrema()[1] == 0 for channel in pixels.split())
```

Repeat for roundedRectExample and pbrSpheresExample and retain all base, head
and diff images. Inspect the canvases as well: identical blank pages are not
valid rendering evidence. `run.py` can run outside a sandbox if localhost or
Chrome GPU processes are blocked inside one.
