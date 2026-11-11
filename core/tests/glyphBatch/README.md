# GlyphBatch regression checks

Build through the core test runner. `allCoreTests glyphBatch` checks unloaded/empty use without a display. `allCoreTests glyphBatch --gpu-check` runs Fbo pixel comparisons and the benchmark in a native window. Pass `--font <path>` to select a local font.

Linux builds with EGL development files also support `LIBGL_ALWAYS_SOFTWARE=1 allCoreTests glyphBatch --egl-check`. This runs the same Fbo checks using a surfaceless Mesa OpenGL context when an X server is unavailable. It does not test native window presentation.

Checks cover horizontal/vertical/TCY/wrapping layout and alignment, ink pivots, 3D EasyCam transforms, minification, atlas growth and queued-image lifetime, RGBA8/RGBA16F/MSAA, blend equations, draw transforms, clearing and retained buffer reuse across device commits. The 3,000/6,000-glyph benchmark compares separate drawString calls, retained batches and batches rebuilt each frame, with three bands. Median/p95 and draw counts are diagnostic; there is no speed assertion.
