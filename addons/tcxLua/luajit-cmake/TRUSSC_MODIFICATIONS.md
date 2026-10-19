# TrussC modifications

Vendored from [zhaozg/luajit-cmake](https://github.com/zhaozg/luajit-cmake)
at commit `94444a6c9bde77a768822f5cd00139161c9de412`.
The existing vendored file set is retained; upstream's Makefile, AGENT.md and
Spanish README are not included.

- `CMakeLists.txt`: retain TrussC's minimum CMake version of 3.16 and
  `CMP0077 NEW`, so the parent addon's `LUAJIT_DIR` variable is respected by
  `option()`. This patch is still needed; upstream uses CMake 3.10.
- All other vendored upstream files are unmodified. The MIT license is unchanged.
