# MSAA resolve after a scissored draw (#861)

Run `allCoreTests msaaResolveScissor --gpu-check` on a display (Xvfb works on
Linux). The default invocation skips display work. The display-test marker
registers this mode in the daily display lane.

Derived from the review's standalone EGL/GLES probe, this regression seeds
the resolve texture red, clears a new pass green, draws green through a 1x1
scissor and ends without resetting it. Every pixel must be exactly green.
Using the clear colour avoids depending on MSAA edge coverage around the clip.
It checks all 768 RGBA pixels,
using samples 1 and 4, with and without mip generation. It also checks copyTo
with an active parent scissor, a repeated clipped pass, and an empty CLEAR
pass with a small raw scissor. A separate single-sample Fbo drawn immediately
after all MSAA cases must have exactly one blue pixel under a 1x1 scissor;
all other pixels stay green, checking restoration of the GL enable state.
All 48 assertions must pass; the samples-4 cases fail on `ca0568bd`.

The test requires RGBA8 4x MSAA support; it asserts the allocated sample count
and does not silently skip or downgrade. It uses `runDisplayTest` so incomplete
execution and failed pixels propagate a nonzero exit on every native backend.
