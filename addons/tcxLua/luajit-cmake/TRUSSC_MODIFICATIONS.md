# TrussC modifications

Vendored from [zhaozg/luajit-cmake](https://github.com/zhaozg/luajit-cmake)
at commit `f0f1650127ba9d6c0efdea49b38c21d3902030d0`.
The existing vendored file set is retained; upstream's Makefile, AGENT.md and
Spanish README are not included.

- `CMakeLists.txt`: the former CMake 3.16 / `CMP0077 NEW` patch is retired.
  The parent `../cmake/LuaJIT.cmake` supplies `CMAKE_POLICY_DEFAULT_CMP0077`;
  the vendor file again uses upstream's CMake 3.10 minimum.
- `LuaJIT.cmake`: the former GDB-JIT option rejection and `lj_gdbjit.c`
  source exclusion are retired vendor edits. Both now live in that parent
  integration, shared with the engine tests. GDB-JIT remains unsupported.
- All vendored upstream files now match the recorded `94444a6` import after
  undoing the documented minimum/policy change. No independent upstream copy
  was available offline. The MIT license is unchanged.

## Update to f0f1650 (2026-11-18)

- Updated only `Utils/zig.toolchain.cmake`: upstream recognizes Zig 0.17
  and adds `-Wno-unused-function` while splitting existing flag assignments.
- Every vendored upstream file matches this commit byte for byte. No vendor
  patches were added; the parent integration and all historical notes above
  are retained. The MIT license is unchanged.
