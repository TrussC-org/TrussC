# LuaJIT source provenance

Source URL: https://github.com/LuaJIT/LuaJIT
Version: 2.1.ROLLING (`src/luajit_rolling.h`).
Exact upstream commit: **not recorded**. Archive SHA-256: **not recorded**.
TrussC import: `4e4bed81`. All 230 tracked import blobs are unchanged, including
DynASM 1.5.0 and the bundled host/minilua sources. This is import equality,
not independent authentication against an upstream revision.

[SHA256SUMS](SHA256SUMS) records every imported file, relative to this directory.
The hashes describe the Linux working-tree bytes; seven `.bat` files use CRLF
under the repository attributes. Verify with `sha256sum -c SHA256SUMS` using
that checkout convention. No generated header/build output is part of the list.
The public `luajit.h` is generated from `src/luajit_rolling.h` by the build.
Its timestamp suffix comes from the enclosing checkout (or build time outside
Git); it must not be treated as proof of the upstream engine revision.

License: MIT; retain [COPYRIGHT](COPYRIGHT), the source notices, and the nested
DynASM/minilua notices. Build integration is owned by `../cmake/LuaJIT.cmake`;
see [the addon ledger](../TRUSSC_MODIFICATIONS.md) for retired source patches.
When updating, record the actual upstream revision and archive hash before
importing, retain notices, regenerate the file hash list, and check both engines.
