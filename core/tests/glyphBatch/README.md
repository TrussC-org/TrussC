# GlyphBatch regression checks

Build through the core test runner. `allCoreTests glyphBatch` checks unloaded/empty use without a display. `allCoreTests glyphBatch --gpu-check` runs Fbo pixel comparisons and the benchmark in a native window. Pass `--font <path>` to select a local font.

Linux builds with EGL development files also support `LIBGL_ALWAYS_SOFTWARE=1 allCoreTests glyphBatch --egl-check`. This runs the same Fbo checks using a surfaceless Mesa OpenGL context when an X server is unavailable. It does not test native window presentation.

Checks cover horizontal/vertical/TCY/wrapping layout and alignment, ink pivots, 3D EasyCam transforms, minification, atlas growth and queued-image lifetime, RGBA8/RGBA16F/MSAA, blend equations, draw transforms, clearing and retained buffer reuse across device commits. The 3,000/6,000-glyph benchmark compares separate drawString calls, retained batches and batches rebuilt each frame, with three bands. Median/p95 and draw counts are diagnostic; there is no speed assertion.

Round 3 also checks repeated draws without another upload, same-frame append/clear snapshots, reusable capacities after warmup and the shared quad index buffer. Benchmark samples commit each device frame and report `sg_make_buffer` initialization counts for warmup and ten measured frames.

Page-order checks use the system font file, falling back to `--font <path>` (relative paths use the data directory). If neither file is available, they print one `SKIP pageOrderChecks` line. The test fills a capped atlas with present glyphs until one lands on page 1, then compares overlapping A/B/A draws and requires three consecutive run draws. The scan stops before U+D800 or after 4,096 nonempty glyphs and fails clearly if no second page appears. `PAGE_ORDER` logs the font source, page count, draw count and selected B codepoint; B is chosen dynamically, not the literal letter B.

Use `allCoreTests glyphBatch --gpu-check --only easycam --dump-diff /tmp/glyphBatch-diff`
for just the two EasyCam comparisons (also supported with `--egl-check`).
`--dump-diff <dir>` is optional and dumps only these two comparisons even in a full run.
The directory is created if needed; each run overwrites its files and `summary.txt`.
Each comparison label becomes a filename with spaces and `/` replaced by `_`:
`<label>-reference.png`, `<label>-result.png`, and `<label>-diff.txt`.
The PNGs use the existing `Pixels::save()` path. Diff rows contain `x y`, reference
and result RGBA byte tuples, and the maximum absolute channel difference; all
nonzero differences are listed, including those within tolerance 2. Coordinates
match the saved PNGs. Glyph indices are omitted: overlapping translucent glyphs
have no unique pixel owner, and no extra ID pass is rendered. Summary rows give
the label, maximum difference, differing pixel count, count above tolerance 2,
and reference lit pixel count (any nonzero channel). The existing `PIXELS` console
counts still count channels rather than pixels.

The depth scene keeps eight rotated glyphs with alpha 0.7. Glyph 0 is drawn first
at Z=24; glyph 1 is drawn at the same X/Y and Z=0, behind glyph 0. Their visible
ink overlaps at both camera distances (200 and 260), so disabling depth on either
the batch or drawString path must fail the comparisons. All rotations, including
the nearly edge-on glyph 6, are unchanged.

`--gpu-check --mip-compare <dir>` saves drawString/GlyphBatch PNGs for 1:1
sizes 12/24/36, uniform minification, 2D rotation, one-axis scaling, 60-degree
Y rotation, near/far perspective, and an Fbo drawn to the window. It repeats
with mipmaps disabled. The original EasyCam scene remains unchanged.
`--gpu-check --mip-bench` reports median-of-three frame times for 3,000 glyphs,
three font families/sizes, with static and newly rasterized coverage. There is
no timing assertion; readback includes completion of GPU work.
`--only easycam --depth-mutant batch` (or `reference`) disables depth on one
path and must fail: this checks that the comparison still detects depth errors.

`--gpu-check --mip-anim <directory>` writes deterministic 192×192 RGBA captures
for 240 steps of uniform scale (0.30 + frame × 0.00125), Y rotation
(40 to 75 degrees), and perspective dolly (distance 400 to 200). Each frame
contains drawString followed by GlyphBatch, both rendered into Fbos and read
back. Additional checkpoint pairs follow: scale 0.30/0.40/0.50/0.60, rotation
45/60/75 degrees, and dolly 400/300/200. Checkpoints are excluded from temporal
metrics; they include the exact endpoints and boundary transforms. The final
regular scale frame is 0.59875, preserving the requested fixed step.

Acceptance compares drawString and GlyphBatch per pixel (maximum absolute
RGBA-channel difference). A frame fails when more than 8 pixels differ by more
than 4, on every frame including checkpoints. No LOD pop is checked the same
way on the second temporal difference of that parity image over the 240 regular
frames: a frame fails when more than 8 pixels jump by more than 8.
The rule counts pixels instead of taking the maximum on purpose. The two paths
build glyph quads differently, so vertex rounding can differ: on Pi 5 (V3D) by
up to 3 inside one glyph, and on D3D11 a quad edge that lands exactly on a pixel
center covers one extra column a frame early (6 pixels, max 37). A split LOD
choice changes whole glyph interiors instead (30 or more pixels at this size:
the hard-threshold build fails with 80 at half scale and 105 at 60 degrees).
`MIP_ANIM` prints this maximum for each sweep; a failed acceptance check makes the mode fail.
This detects path-specific policy jumps, not shimmer shared by both paths.
Per-path second-difference max/p95 and pop-pixel counts remain diagnostics,
not pass/fail criteria.
