# LZ4 provenance

Upstream: https://github.com/lz4/lz4, release tag `v1.11.1`.
Only `lib/lz4.c` and `lib/lz4.h` are vendored, unchanged, as `lz4.c` and
`lz4.h`. No TrussC patches or patch markers were present in the previous
1.10.0 copy; none need to be reapplied.

The `v1.11.1` release archive (`lz4-1.11.1.tar.gz`) has SHA-256
`683fed08a844ad012196381e40badf80c0bbe4962959761416d41c9e67d77ec7`.

| Vendored file | SHA-256 |
| --- | --- |
| `lz4.c` | `2e58f124f1032f15aecc99772b7de624c4289245729fa5403506f00b06a54937` |
| `lz4.h` | `c204caad0638a12c9ce80831094ec8fffb34aa57e74307067b08fc9452fc2611` |

License: BSD 2-Clause. The archive's `lib/LICENSE` is unchanged from the
LZ4 license text reproduced in `docs/LICENSE.md`; retain the copyright and
license notices in both source files.

To update, read upstream `NEWS`, replace only these two files from the release,
check for local patches, update these hashes and the LZ4 row in
`docs/LICENSE.md`, then run `tools/check_dependencies.py`, the dependency
tests in `tools/tests`, `core/tests/compressBounds`, and the headless
`addons/tcxDepthRecord/tests` suite. The wrapper uses the block API only;
frame, HC, CLI, and upstream build files are not vendored.
