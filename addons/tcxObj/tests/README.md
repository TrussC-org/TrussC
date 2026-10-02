# tcxObj tests

Headless console test (no window). Each case writes a small `.obj` to a temp
directory and loads it with `ObjLoader`, checking that face indices are
validated before they are read:

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
