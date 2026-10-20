# grabScreenExample

Press **Space** to capture the finished frame. The `EventListener` member keeps
the `events().afterFrame` subscription alive. Its callback reads into `Pixels`
and uploads a `Texture`; the next `draw()` displays the thumbnail and its pixel
dimensions. Later grabs include the previous thumbnail.

`afterFrame` runs after the frame has finished, including ImGui if used. Drawing
to the screen from this listener will not appear. Use it to read back the frame
or render into an `Fbo`, and display the result in a later `draw()`.

From the repository root, generate the project and build on Linux:

```sh
tools/bin/trusscli update -p examples/graphics/grabScreenExample --tc-root . --ide cmake
cmake -S examples/graphics/grabScreenExample -B examples/graphics/grabScreenExample/build-linux -G "Unix Makefiles"
cmake --build examples/graphics/grabScreenExample/build-linux --config Release -j 2
examples/graphics/grabScreenExample/bin/grabScreenExample
```

Pass `--self-check` to request one grab after five frames, check the framebuffer
dimensions, rectangle and background colors, and thumbnail upload, then exit
with status 0 on success or 1 on failure. On Linux with Xvfb:

```sh
LIBGL_ALWAYS_SOFTWARE=1 __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json xvfb-run -a examples/graphics/grabScreenExample/bin/grabScreenExample --self-check
```

The daily CI example sweep (`examples/build_all.py --verbose`) discovers this
folder automatically through its `src/` directory; there is no separate list.
