# ImGui pass-break cost (#713 Q3)

Build this addon test, then run from its project directory:

- Air: `bin/tests-passBreakCost.app/Contents/MacOS/tests-passBreakCost --measure`
- Pi/Linux: `bin/tests-passBreakCost --measure`

On Linux, export `LIBGL_ALWAYS_SOFTWARE=1` and
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json` before
starting Xvfb, then run under that display. With no arguments it safely skips.

The four arms render the same UI and preceding/following rectangles on a window
and an Fbo, uploading either before the pass or at the UI draw point with a
suspend/flush/resume. Each arm warms up for 30 frames and measures 120 frames.
Output reports draw-to-afterFrame wall time (including submission/present), mean,
median, p95, and pass count. It also reports `completed_ms_*`: the same interval
through synchronous target readback, including GPU completion and readback cost.
The paired arms use identical readbacks; these numbers expose GPU pass-break
work that asynchronous submission can hide. These are measurements, with no
timing assertions; vsync is disabled. Compare the two arms on the same
device/backend. The Fbo uses its default RGBA8 format; the window uses its
platform default.
The target pipeline adapter selects the actual attachment format.
