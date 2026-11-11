# TrussC modifications

Vendored from [zhaozg/luajit-cmake](https://github.com/zhaozg/luajit-cmake)
at commit `94444a6c9bde77a768822f5cd00139161c9de412`.
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
