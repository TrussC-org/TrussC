# GlyphBatch regression checks

Build through the core test runner. `allCoreTests glyphBatch` checks unloaded/empty use without a display. `allCoreTests glyphBatch --gpu-check` runs Fbo pixel comparisons and the benchmark in a native window. Pass `--font <path>` to select a local font.

Linux builds with EGL development files also support `LIBGL_ALWAYS_SOFTWARE=1 allCoreTests glyphBatch --egl-check`. This runs the same Fbo checks using a surfaceless Mesa OpenGL context when an X server is unavailable. It does not test native window presentation.

Checks cover horizontal/vertical/TCY/wrapping layout and alignment, ink pivots, 3D EasyCam transforms, minification, atlas growth and queued-image lifetime, RGBA8/RGBA16F/MSAA, blend equations, draw transforms, clearing and retained buffer reuse across device commits. The 3,000/6,000-glyph benchmark compares separate drawString calls, retained batches and batches rebuilt each frame, with three bands. Median/p95 and draw counts are diagnostic; there is no speed assertion.

Round 3 also checks repeated draws without another upload, same-frame append/clear snapshots, reusable capacities after warmup and the shared quad index buffer. Benchmark samples commit each device frame and report `sg_make_buffer` initialization counts for warmup and ten measured frames.

Page-order checks use the system font file, falling back to `--font <path>` (relative paths use the data directory). If neither file is available, they print one `SKIP pageOrderChecks` line. The test fills a capped atlas with present glyphs until one lands on page 1, then compares overlapping A/B/A draws and requires three consecutive run draws. The scan stops before U+D800 or after 4,096 nonempty glyphs and fails clearly if no second page appears. `PAGE_ORDER` logs the font source, page count, draw count and selected B codepoint; B is chosen dynamically, not the literal letter B.
