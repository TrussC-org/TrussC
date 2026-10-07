# tinyobjloader bundled with tcxObj

Provenance for `src/tiny_obj_loader.h`: where the copy comes from, what
TrussC changed in it, and how to update it. tinyobjloader is by Syoyo Fujita
and contributors, under the MIT license (see [LICENSES.md](LICENSES.md)).

| File | Version | Source | Commit | Imported | TrussC patches |
|---|---|---|---|---|---|
| `src/tiny_obj_loader.h` | 2.0.0 (last entry of the version history in the file) | [tinyobjloader/tinyobjloader](https://github.com/tinyobjloader/tinyobjloader), branch `release` | `966edceaf8cdca7996c4e9a1c5ced2938de63366` (2026-03-10) | 2026-04-19 | 2 |

"Commit" is the commit of the source repository whose copy of the file is
byte-identical to TrussC's copy before TrussC patches (git blob
`af98ac2d3d493a5868042f57c755167cc5927ceb`). The file stayed unchanged on
`release` until `62ff207968f3dc14a64a1e2378dce67b760e7c4a` (2026-05-22,
"Add optimized parser with multithreading, SIMD, and custom allocator
support"), which TrussC has not taken.

The implementation is compiled in `src/tcxObjLoader.cpp`
(`TINYOBJLOADER_IMPLEMENTATION`), which calls `tinyobj::LoadObj()` with its
defaults (triangulate on). No other configuration macro is set; in
particular `TINYOBJLOADER_USE_MAPBOX_EARCUT` is not, so polygons with more
than four vertices go through the built-in ear clipping.

## TrussC patches

Each is marked `// TrussC patch:` in the file.

1. `tinyobj_ff::distance` (in the embedded fast_float) is `constexpr`:
   fast_float calls it from a `FASTFLOAT_CONSTEXPR20` function, and newer
   MSVC refuses the build otherwise (#77).
2. Triangulation compares vertex indices against the position array by
   division (`vi >= v.size() / 3`), a size_t-safe comparison. Changed in the
   quad split, in the built-in ear clipping (the axis search, the ear
   candidate and the overlap test), and in the
   `TINYOBJLOADER_USE_MAPBOX_EARCUT` path (its index check and `assert`),
   which TrussC does not compile but is kept consistent. Valid faces
   triangulate as before, since the position array always holds whole xyz
   triples.

**Covered by**: `tests/`: a valid quad and a valid concave pentagon (the
result is unchanged), and a quad and a pentagon with a vertex index one
past the end of the positions, which reach the patched checks (a triangle
is passed through without them). The test checks that tinyobjloader drops
the quad with its warning, and the pentagon's vertex order reaches all
three ear clipping checks; run it under AddressSanitizer as well to check
that no position past the array is read.

## Updating

1. Fetch the `release` branch and pick one commit. Look at what changed in
   the file since the pinned commit (`git log -p <pinned>..<new> --
   tiny_obj_loader.h`).
2. Copy the file over `src/tiny_obj_loader.h` unchanged.
3. Reapply the TrussC patches listed above (`grep -n "TrussC patch"` in the
   old copy finds them), unless the new file already contains an equivalent
   change. Keep each marker comment.
4. Check that `src/tcxObjLoader.cpp` still compiles against it, and run the
   addon tests (`build_all.py --addon-tests-only`) and `example-basic`.
5. Update this file (commit, date, version, patches) and the tinyobjloader
   row of [docs/LICENSE.md](../../docs/LICENSE.md#third-party-libraries)
   (`tools/check_dependencies.py` compares the two).

To check which upstream commit the file matches, undo the TrussC patches
and run in a clone of tinyobjloader: `git log --all -m --format='%H %ci'
--find-object=$(git hash-object <unpatched copy>)` and take the oldest line.
