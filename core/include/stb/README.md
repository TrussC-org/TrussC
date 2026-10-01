# stb libraries bundled with TrussC

Provenance for every stb file in TrussC: where each copy comes from, what
TrussC changed in it, and how to update it. All of them are by Sean Barrett
and contributors, under the stb dual license (MIT or Public Domain, see
[LICENSE](LICENSE)).

| File | Version string | Source | Commit | Imported | TrussC patches |
|---|---|---|---|---|---|
| `stb_image.h` | v2.30 | [nvpro-samples/stb](https://github.com/nvpro-samples/stb), branch `nv/all-fixes` | `1cafe0e01eeaf4142f05b36218793cb46c7ce433` (2025-12-24) | 2026-09-30 | 2 |
| `stb_image_write.h` | v1.16 | [nothings/stb](https://github.com/nothings/stb), `master` | `1ee679ca2ef753a528db5ba6801e1067b40481b8` (2021-07-11) | 2025-12-16 | 1 |
| `stb_perlin.h` | v0.5 | [nothings/stb](https://github.com/nothings/stb), `master` | `2bb4a0accd4003c1db4c24533981e01b1adfd656` (2020-02-02) | 2025-12-16 | none |
| `stb_truetype.h` | v1.26 | [nothings/stb](https://github.com/nothings/stb), `master` | `6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760` (2024-07-15) | 2025-12-16 | 3 |
| `../stb_vorbis.c` | v1.22 | [sezero/stb](https://github.com/sezero/stb), branch `stb_vorbis-sezero` | `dd0c5eccaf092012b531d69d595fb587ece79571` (2026-07-13) | 2026-09-30 | none |

`stb_vorbis.c` lives one level up, in `core/include/`, because
`core/include/tc/sound/tcSound_impl.cpp` includes it as `"stb_vorbis.c"`.

The implementations are compiled in `core/include/impl/stb_impl.cpp`
(`stb_image`, `stb_image_write`, `stb_perlin`, `stb_truetype`, with
`STBI_WINDOWS_UTF8` / `STBIW_WINDOWS_UTF8`, and `STBTT_assert`,
`STBTT_malloc` / `STBTT_free` for stb_truetype, see below) and in
`core/include/tc/sound/tcSound_impl.cpp` (`stb_vorbis`, inside `extern "C"`).
No other configuration macro is set: every stb_image format is enabled.

"Commit" is the commit of the source repository whose copy of the file is
byte-identical to TrussC's copy before TrussC patches. For the three
nothings/stb files it is the latest upstream commit that touched the file;
the file is unchanged from there to upstream `master` as of 2026-08-01
(`2c980bb59875b0d32144a71867fbdebb2f77cd20`).

## stb_image.h

- **Source**: nvpro-samples/stb, branch `nv/all-fixes`, commit
  `1cafe0e01eeaf4142f05b36218793cb46c7ce433`. `stb_image.h` itself was last
  changed in that branch by `1450fb761bceaeb5464452bd8e57a134c3f4466e`.
  This is a fork of nothings/stb that collects robustness fixes for malformed
  files that upstream has not merged. The public API and the set of formats
  are the same as upstream v2.30.
- **Before 2026-09-30**: unmodified upstream nothings/stb v2.30,
  `013ac3beddff3dbffafd5177e7972067cd2b5083` (2024-05-31).
- **TrussC patches** (each marked `// TrussC patch:` in the file):
  1. `stbi__bmp_load`: the palette array `pal[256][4]` is zero-initialized,
     so a BMP pixel that indexes past the stored palette entries reads as
     black.
  2. `stbi__out_gif_code`: the LZW prefix chain is collected into a fixed
     4096-entry buffer and written out with a loop, instead of one recursive
     call per link. The per-pixel body moved into a new helper,
     `stbi__out_gif_pixel`. Output is unchanged.
- **Covered by**: `core/tests/mediaDecode` (every format, plus one check per
  patch).

## stb_image_write.h

- **Source**: nothings/stb `master`, the file as of
  `1ee679ca2ef753a528db5ba6801e1067b40481b8`.
- **TrussC patches** (marked `[TrussC]` in the file):
  1. `stbi_write_hdr_core`: `sprintf` into the header buffer became
     `snprintf(buffer, sizeof(buffer), ...)`, to silence the deprecation
     warning of the macOS SDK. Upstream has an open pull request for the same
     change (nothings/stb#1887); drop the patch once upstream has it.

## stb_perlin.h

- **Source**: nothings/stb `master`, the file as of
  `2bb4a0accd4003c1db4c24533981e01b1adfd656`. No TrussC patches.

## stb_truetype.h

- **Source**: nothings/stb `master`, the file as of
  `6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760` (a merge commit; the version
  history in the file ends at 1.26, 2021-08-28).
- **TrussC patches** (marked `// TrussC patch:` in the file):
  1. `stbtt_InitFont_internal`: the CFF buffer is made with the `CFF ` table
     length from the table directory, so CFF data (INDEX, DICT, Subrs,
     FDSelect, CharStrings) is read within the CFF table's length.
  2. CFF glyph outlines (`stbtt__csctx`, `stbtt__csctx_v`,
     `stbtt__run_charstring`, `stbtt__GetGlyphShapeT2`): vertex counting
     stops within the range stb handles (int count, size_t allocation), and
     the allocation result is checked. The counting pass stops at
     `STBTT_MAX_VERTICES`, by default
     `min(INT_MAX, SIZE_MAX / sizeof(stbtt_vertex))`: `stbtt__csctx_v` sets
     a new `stopped` field, and `stbtt__run_charstring` returns 0 at the top
     of its loop (subroutine calls run in that loop) and at `endchar` once it
     is set. `STBTT__CSCTX_INIT` gained the matching
     initializer. The glyph then has no outline. `stbtt__GetGlyphShapeT2`
     returns 0 vertices when `STBTT_malloc` returns NULL.
     `STBTT_DEFAULT_MAX_VERTICES` is that limit; `STBTT_MAX_VERTICES`
     defaults to it and may be predefined.
  3. Glyph rasterization (`stbtt_FlattenCurves`, `stbtt__rasterize`): the
     flattened point count and the edge allocation (one edge per point plus
     a sentinel) stay within the range stb handles (int count, size_t
     allocation). `STBTT__MAX_EDGES` is
     `min(INT_MAX, SIZE_MAX / sizeof(stbtt__edge))`. One vertex adds at most
     `STBTT__MAX_POINTS_PER_VERTEX` (2^16) points, since a curve is split at
     most 16 levels deep, so `STBTT_DEFAULT_MAX_POINTS` is
     `STBTT__MAX_EDGES - 1 - 2^16`. The counting pass of
     `stbtt_FlattenCurves` stops after the vertex that takes the count past
     `STBTT_MAX_POINTS` (which defaults to `STBTT_DEFAULT_MAX_POINTS` and may
     be predefined); the glyph then gets no points and is not drawn.
     `stbtt__rasterize` draws nothing when the point counts add up past
     `STBTT__MAX_EDGES - 1`.
- **Configuration** (in `core/include/impl/stb_impl.cpp`, not a change to the
  file): `STBTT_assert` does nothing in every build type, so Debug and
  Release builds handle font data the same way. Allocations through
  `STBTT_malloc` are padded with 64 zeroed bytes, and it returns NULL when
  the size cannot be allocated.
  `STBTT_MAX_VERTICES` and `STBTT_MAX_POINTS` read variables that default
  to `STBTT_DEFAULT_MAX_VERTICES` and `STBTT_DEFAULT_MAX_POINTS`; the test
  hook `internal::setStbttLimitsForTests()` (declared in
  `core/include/tc/graphics/tcFont.h`) lowers them and caps `STBTT_malloc`
  sizes, and `internal::resetStbttLimitsForTests()` restores the defaults.
- **Checked by TrussC**: `FontAtlasManager`
  (`core/include/tc/graphics/tcFont.h`) checks the sfnt skeleton (collection
  header, table directory, table bounds, required tables, the fixed fields
  stb reads, hmtx, and loca bounds and order) against the data size before
  `stbtt_InitFont`, and after it that the CFF CharStrings INDEX count can be
  read within the CFF table. A glyph index from the cmap past `numGlyphs`
  (for CFF, past the CharStrings count) and a codepoint above U+10FFFF map
  to .notdef. Fonts of 1 GiB or more are refused. cmap subtable contents,
  glyph outlines and composite glyph nesting are not checked.
- **Covered by**: `core/tests/fontSfntCheck`.

## stb_vorbis.c (in core/include/)

- **Source**: sezero/stb, branch `stb_vorbis-sezero`, commit
  `dd0c5eccaf092012b531d69d595fb587ece79571`. This is a fork of nothings/stb
  that collects robustness fixes for malformed files that upstream has not
  merged. The version string is still v1.22; the API TrussC calls
  (`stb_vorbis_open_file`, `stb_vorbis_open_memory`, `stb_vorbis_get_info`,
  `stb_vorbis_stream_length_in_samples`,
  `stb_vorbis_get_samples_float_interleaved`, `stb_vorbis_close`) is the same
  as upstream.
- **Before 2026-09-30**: unmodified upstream nothings/stb v1.22,
  `1ee679ca2ef753a528db5ba6801e1067b40481b8` (2021-07-11).
- **TrussC patches**: none.
- **Covered by**: `core/tests/mediaDecode` (file and memory decode) and
  `core/tests/audioDiagnostics` (a failed `stb_vorbis_open_file` with
  `close_on_free` closes the `FILE` once; `SoundBuffer::loadOgg` relies on
  that).

## Updating

1. Fetch the source branch from the table and pick one commit. Look at what
   changed in the file since the pinned commit (`git log -p <pinned>..<new> --
   <file>`), and for the two forks, also whether upstream nothings/stb
   `master` has moved on in a way the fork has not picked up yet.
2. Copy the file over TrussC's copy unchanged.
3. Reapply the TrussC patches listed above for that file (`grep -n "TrussC"`
   in the old copy finds them), unless the new source already contains an
   equivalent change. Keep each marker comment.
4. Check that TrussC still compiles against it: `grep -rn "stbi_\|stb_vorbis_\|stbtt_\|stb_perlin_"`
   over `core/`, `addons/` and `examples/` lists the functions in use.
   Include sites: `core/include/impl/stb_impl.cpp`,
   `core/include/tc/sound/tcSound_impl.cpp`,
   `core/include/tc/graphics/tcPixels.h`, `core/include/tc/graphics/tcFont.h`,
   `core/include/tc/math/tcNoise.h`, `core/include/tc/utils/tcStandardTools.h`,
   `core/platform/win/tcPlatform_win.cpp`,
   `core/platform/linux/tcVideoGrabber_linux.cpp`.
5. Run the core tests (`build_all.py --core-tests-only`; `mediaDecode` and
   `audioDiagnostics` cover these files) and build `AllFeaturesExample`.
6. Update this file (commit, date, version string, patches) and the stb rows
   in [docs/LICENSE.md](../../../docs/LICENSE.md).

To check which upstream commit a nothings/stb file matches, run in a clone
of nothings/stb: `git log --all -m --format='%H %ci' --find-object=$(git
hash-object <TrussC copy>)` and take the oldest line (`-m` is needed when the
file last changed in a merge, as `stb_truetype.h` did). For
`stb_image_write.h` and `stb_truetype.h`, undo the TrussC patch first.
