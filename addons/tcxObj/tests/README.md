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
  `モデル.obj` writes a UTF-8 `mtllib` name and loads again with its texture;
- exporter saves return `SaveResult`; unwritable folders return `AccessDenied`
  with one Error (POSIX only, skipped as root);
- blocked OBJ, MTL and PNG temporary paths leave previous files intact, log
  one Error and clean up generated temporaries without removing the blocker;
- empty exporters, paths and textures fail with one Error; successful retries,
  baked textures and geometry-only exports work without leftover temporaries.

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

The load-entry tests check `FileNotFound` and the exact shared missing-file
Error, `DecodeFailed` with exactly one Error for a broken OBJ, and relative
Japanese OBJ/MTL/texture paths through `getDataPath()`. Fixtures are generated
at runtime.

Model table tests accept upstream-compatible tag counts,
preserve upstream’s silent truncation to 8192 values per kind for direct
vendor callers retaining tags, accept tag values starting with `#`, retain
upstream tag payloads across groups, recover from single and
persistent injected allocation failures, and load local OBJ and MTL files above
the former stream threshold. Pending tinyobj Warnings are delivered before
the Error when a later OBJ statement rejects the model.
Unsupported or malformed MTL statements warn and keep usable geometry, like a
missing MTL. Tests cover `d -halo`, `Tf xyz`, `Ka spectral` and an allocation
failure while opening an MTL: successful geometry without materials must warn.
If diagnostics also cannot allocate, addon recovery returns failure. Fixtures and
allocation-size observations are generated in the test process. Single and persistent
sweeps cover every observed request for two textured groups and point-only OBJ,
with core context warmed before injection and a successful retry after each fault.

Under persistent allocation exhaustion, failure text and logging are best effort;
addon recovery does not require a diagnostic allocation. Pre-existing core
GPU-uploaded model reload can still terminate if deferred GPU-buffer retirement
cannot allocate. Cold-start Material destruction can initialize core context and
allocate (code trace only). These core paths are outside addon recovery.
LogStream delivery failures are guarded in core.

OOM recovery is supported in Release builds, or with iterator debugging off (`_ITERATOR_DEBUG_LEVEL=0` on MSVC). With iterator debugging enabled, constructing or moving even an empty `LoadResult` / `SaveResult` string or STL container can allocate a container proxy, so persistent allocation failure can terminate in a `noexcept` operation. Diagnostic text and logging remain best effort.

STL-wide persistent allocation-failure cases SKIP when `_ITERATOR_DEBUG_LEVEL != 0`, with the reason printed. Targeted one-shot payload failures still run. Exhaustive one-shot STL sweeps, including the optional-MTL open regression, SKIP too: they deliberately target noexcept container proxy construction and moves, which can terminate even after a single refused proxy allocation. MSVC Debug CRT reports go to stderr and abort dialogs are disabled by the shared test support header.
