# tinyobjloader bundled with tcxObj

Provenance for `src/tiny_obj_loader.h`: where the copy comes from, what
TrussC changed in it, and how to update it. tinyobjloader is by Syoyo Fujita
and contributors, under the MIT license (see [LICENSES.md](LICENSES.md)).

| File | Version | Source | Commit | Imported | TrussC patches |
|---|---|---|---|---|---|
| `src/tiny_obj_loader.h` | 2.0.0 (last entry of the version history in the file) | [tinyobjloader/tinyobjloader](https://github.com/tinyobjloader/tinyobjloader), branch `release` | `45636bdcef1a4fec140346b90c0b50bf0bc3e23b` (2026-06-19) | 2026-10-08 | 5 |

"Commit" pins the `release` branch snapshot used for this update. Before
TrussC patches, the header is byte-identical to that snapshot. The patches below still apply; none has been replaced by an upstream equivalent.

The latest tag, `v2.0.0rc13` (2024-01-26), predates the previous vendored
commit `966edce`. Upstream's [release notes](https://github.com/tinyobjloader/tinyobjloader/releases)
recommend the `release` branch; this update follows issue #639's Decision.

## Upstream changes since `966edce`

Reviewed the [complete commit interval](https://github.com/tinyobjloader/tinyobjloader/compare/966edceaf8cdca7996c4e9a1c5ced2938de63366...45636bdcef1a4fec140346b90c0b50bf0bc3e23b)
and the [README changelog](https://github.com/tinyobjloader/tinyobjloader/blob/45636bdcef1a4fec140346b90c0b50bf0bc3e23b/README.md#whats-new).
There are no intervening release tags.

- Parser hardening includes a joint-ID range check in the classic loader.
  The new C11 loader also received allocator and I/O callback hardening;
  those separate C files are not vendored by TrussC.
- The C++ header adds `LoadObjOpt` / `LoadObjOptTyped`, arena allocation and
  opt-in SIMD, threading and exceptions. TrussC continues using `LoadObj`
  with its existing defaults; it does not select the new optimized API.
- Tag and skin-weight types now have allocator-aware implementations with
  the existing names retained as default-allocator aliases. The header
  also guards its implementation against repeated inclusion.
- The remaining commits update the README and introduce/document the
  separate C11 loader. The MIT license text is unchanged.

The implementation is compiled in `src/tcxObjLoader.cpp`
(`TINYOBJLOADER_IMPLEMENTATION`), which calls the implementation-only
`tinyobj::detail::LoadObjWithoutTags()` entry with triangulation on (patch 5
below). Public tinyobj declarations and signatures remain upstream-compatible.
No other configuration macro is set; in
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

3. Remove the 256 MiB `StreamReader` cap and the
   `TINYOBJLOADER_STREAM_READER_MAX_BYTES` macro, including its documentation
   and mmap checks. This affects default direct callers as well as TrussC,
   including `std::istream` callers with non-local sources; defining the old
   macro no longer limits input. Buffered reads still check vector and stream
   widths. Every mmap deletion site has a patch marker.
4. Validate tag count representation, same-line value availability and output
   widths before resizing or discarding. Negative/unrepresentable counts and
   declarations exceeding their available tokens reject the OBJ, whereas
   upstream clamps or supplies defaults. Preserve upstream's slash whitespace,
   omitted fields, values starting with `#`, and the 8192-value clamp for
   retained payloads. Direct callers retaining tags get upstream's silent
   truncation to 8192 values per kind. Check MTL
   index representation before narrowing to int; an unrepresentable index throws
   `length_error` and the addon rejects the unit. Material syntax/parse errors
   remain optional-material diagnostics, handled as Warnings by the addon.
5. An implementation-only `detail::LoadObjWithoutTags` entry selects the
   internal tag opt-out for `ObjLoader`, without changing any public tinyobj
   signature or default tag output. It validates the full tag declaration and
   discards unused payloads before allocation or per-shape copies, so the
   framework has no tag count cap. Tests compare actual framework allocations
   across groups and separately check default retained-tag output.

The default-path differences from upstream are patches 1–4 above: compiler
compatibility, index validation, removal of the stream cap/configuration macro,
and structural/representation rejection of invalid tag/material counts. Patch 5
changes only the explicit implementation-only framework entry. Valid tag
whitespace and incomplete triples keep upstream acceptance.

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
