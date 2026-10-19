# grabScreen destination reuse

The default run needs no display. Run the actual capture regression with
`bin/grabScreenReuse --screen` (or the `display-test` runner) under a display.

Destination reuse applies on Linux/Pi, Android, Windows and iOS. The macOS
synchronous path instead moves a fresh per-call result into the destination
only on success, so a timed-out capture's late completion never writes to the
caller's `Pixels`.

The test captures two different, two-color scenes into the same `Pixels`,
checks that the data pointer stays unchanged without array allocation/release,
and compares every RGBA byte. On macOS the pointer/allocation stability checks
print SKIP with the timeout-safety reason above. The post-resize content
checks also print SKIP on macOS until #911 (the first frame after a resize
renders with the old viewport) is fixed. All other content checks, and the
layout, resize and format-mismatch reallocation checks, run on every platform.
Resizing the window from 200x160 to 280x220 must replace the destination
buffer and produce matching content and dimensions. Both sizes stay above the
Windows minimum client width, and the test waits for whichever new size the
platform applies rather than an exact match. RGB and F32 destinations
of matching dimensions must also be replaced with RGBA8 storage.

On Linux/Android GL, it also injects an unattached framebuffer as the window
capture source after a successful capture, while a different, complete caller
framebuffer is bound and hostile pixel-pack state is set. `grabScreen` must
return false, report one Error with `GL_INVALID_FRAMEBUFFER_OPERATION` (`0x506`), preserve the matching allocation,
and leave retained pixels unflipped. Both caller read/draw bindings and pack
state, and the source's read-buffer selection, must be restored on failure.
Restoring the source permits a new capture even with an unrelated GL error
pending, matching Fbo readback. These GL failure checks remain enabled on
Linux/Android; they are independent of the macOS stability skip.

This test has its own binary because it observes global array allocations
and releases. Pointer equality alone cannot detect reallocation when the
allocator returns the same address; likewise, resizing may legitimately
reuse the old address. The allocation/release probes cover both cases.

On Linux, after building:

```sh
LIBGL_ALWAYS_SOFTWARE=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
xvfb-run -a -s '-screen 0 1280x1024x24' bin/grabScreenReuse --screen
```
