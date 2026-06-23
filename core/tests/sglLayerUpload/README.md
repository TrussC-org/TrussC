# sglLayerUpload — sokol_gl per-frame single-upload regression test

Standalone, headless regression test for the sokol_gl (`sokol_gl_tc.h`) vertex
buffer blow-up fixed on branch `fix/buffer-increase-bug`.

## The bug it guards

`flushDeferredShaderDraws()` (swapchain) and `flushFboDeferredPbr()` (FBO) draw
each sokol_gl **layer** with `sgl_(context_)draw_layer()`. Every deferred PBR /
shader draw bumps the layer counter, so a busy frame flushes many layers (N).

The TrussC fork's `_sgl_draw()` used `sg_append_buffer()` and re-appended the
**entire** recorded vertex set (V vertices) on *every* call. So one frame uploaded
`N × V` vertices, and `_sgl_grow_gpu_buffer()` doubled the GPU buffer to keep up
(2.5 → 5 → 10 → … MB) until the backend refused the allocation:

```
[sg][error][id:52] METAL_CREATE_BUFFER_FAILED
```

…after which that sgl context renders nothing for the rest of the run (deferred 2D
and PBR content silently vanishes). Low-vertex scenes never hit it; it needs both
a high layer count and a non-trivial vertex count.

**The fix:** `_sgl_draw()` appends the vertex set **once per frame** and reuses
the returned base offset for the remaining layer draws — O(V), not O(N×V). See
mod #10 in `core/include/sokol/TRUSSC_MODIFICATIONS.md`.

## What CI checks automatically (this test)

This test drives sokol_gl on `SOKOL_DUMMY_BACKEND` — **no GPU, no window, no
frameworks**. The dummy backend has no real device but still tracks the exact
numbers that blew up: `append_pos` (bytes uploaded this frame) and the GPU buffer
size. So the regression is fully deterministic and runs on every OS in CI.

It asserts:
1. Empty layers → exactly **one** upload per frame (`append_pos == V`), not N×V.
2. GPU buffer does **not** grow as the layer count grows.
3. Non-empty layers → the single upload covers the **full** accumulated set.
4. A new frame **re-uploads** (cache keyed on frame id, not stale).
5. `sgl_tc_context_reset()` **invalidates** the cache (no stale base offset reuse).

It is **NOT** a render-correctness test — it cannot see pixels. It only proves the
upload *accounting* is fixed. Pixel correctness needs real GPUs (see below).

### Run it

CI runs it via the core-test gate (it is auto-detected as a standalone test —
committed `CMakeLists.txt`, no `src/`):

```bash
python3 examples/build_all.py --core-tests-only --verbose
```

Or directly:

```bash
cd core/tests/sglLayerUpload
cmake -S . -B build && cmake --build build
./build/sglLayerUpload        # Windows: build\Release\sglLayerUpload.exe
```

Expected tail on success (exit code 0):

```
empty layers: exactly one upload per frame (no N*V)          PASS
empty layers: GPU buffer does not grow with layer count      PASS
non-empty layers: one upload of the full vertex set          PASS
new frame re-uploads its vertices (not stale)                PASS
context reset invalidates cache (re-appends, no stale reuse) PASS

PASSED (0 failures)
```

Against the pre-fix `sokol_gl_tc.h`, checks 1–4 FAIL with `append_pos` many times
`V` — that is the bug, and the reason this test exists.

## What still needs a REAL device (NOT covered by CI)

CI proves the *accounting* is fixed on all backends, but it cannot prove that the
reused base offset still draws the **correct pixels** on a real GPU. That part was
verified manually:

- **Metal / macOS** — verified (PBR meshes + sokol_gl 2D render correctly).
- **D3D11 / Windows** and **GL / Linux** — please verify on real hardware.

### Manual real-device check

There is no automated GPU test (the core tests are headless by charter). To check
a backend by hand, run any app that, **per frame**, issues *many* deferred draws
(each deferred PBR mesh or `tc::Shader` draw bumps an sgl layer) **together with**
a non-trivial amount of sokol_gl 2D geometry (e.g. lots of `drawRect`/lines, a big
`drawBitmapString`, or filled paths). A few hundred deferred draws + thousands of
2D vertices is enough.

- **Before the fix:** within seconds the log prints `id:52 METAL_CREATE_BUFFER_FAILED`
  (or the D3D11/GL equivalent buffer-allocation failure) **once**, and the deferred
  2D/PBR content disappears for the rest of the run.
- **After the fix:** no `id:52`, content persists, and GPU memory stays flat.

A quick way to manufacture the load: take a 3D PBR example, draw a few hundred
meshes (so layer count climbs) and overlay a large amount of 2D each frame, then
let it run for ~30 s while watching stderr for buffer-allocation errors.
