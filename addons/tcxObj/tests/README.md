# tcxObj tests

Headless console test (no window). Each case writes a small `.obj` to a temp
directory and loads it with `ObjLoader`, checking face index validation,
computed normals and UTF-8 paths:

- valid faces load as before: positive and relative (negative) indices,
  normals and texcoords, quads split into two triangles, a concave pentagon
  split into three by tinyobjloader's ear clipping;
- a face vertex index of 0, or a negative index before the first vertex, fails
  the load (tinyobjloader rejects the line) and logs an error;
- a face whose vertex, normal or texcoord index is past the end of its list is
  skipped with one warning; the other faces still load;
- a quad or pentagon with a vertex index one past the end reaches
  tinyobjloader's own checks in triangulation: the quad is dropped with
  tinyobjloader's warning, and ear clipping of the pentagon never reads the
  missing vertex (AddressSanitizer builds check this); ObjLoader then skips
  the triangles that use it;
- a normal index in a file with no normals is ignored, as before;
- computed normals point in the same direction at scales of 0.001 and 100,
  have unit length and retain the 1:4 area weighting of two adjoining faces;
  degenerate faces and cancelling face normals keep the `(0, 0, 1)` fallback;
- mixed normals retain the file's exact values and compute only missing entries,
  including shared vertices with area weighting and degenerate faces;
- mixed texcoords keep their vertex alignment and use `(0, 0)` for missing
  entries, in either face order and within a single face;
- a Japanese-named folder, OBJ, MTL and PNG load together; exporting
  `モデル.obj` writes a UTF-8 `mtllib` name and loads again with its texture.

Texture cases use sokol's dummy graphics backend (no window or GPU). On
Windows, the test executable uses the UTF-8 manifest from `trussc_app()`.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```

To also check that the issue #437 mesh queues a valid lit PBR draw with a
material, configure with `-DTCX_OBJ_TEST_GPU=ON`, rebuild and run the same
executable in a graphics session (under Xvfb on Linux). This optional mode uses
the native graphics backend and runs all cases inside one draw callback.
The default mode remains headless and needs no display.
