# TrussC modifications

## Engine source provenance

The L1/L2 binary-reader patches and both `tcloadbuffer.h` files were removed.
The L3 MSVC source rewrite in `lj_gdbjit.c` was also removed. Each restored
file is byte-identical to the imported source before `cc1f0733` (#969), recovered
with `git show cc1f0733^:<path>` without network access.

PUC Lua is 5.4.9 (import update `9feb00f5`). LuaJIT is 2.1.ROLLING,
imported in `4e4bed81`; its exact upstream commit was not recorded. Therefore
LuaJIT identity against a separately downloaded upstream commit cannot be
verified offline. Original MIT copyright notices are retained.

| Restored file | Imported upstream version | SHA-256 |
| --- | --- | --- |
| `LuaJIT/src/lj_bcread.c` | LuaJIT 2.1.ROLLING (upstream commit unrecorded) | `653b2572f30ac7847949b3e978f42a2c84487eddaae5566bd4f260091656ab94` |
| `LuaJIT/src/lj_lex.c` | LuaJIT 2.1.ROLLING (upstream commit unrecorded) | `49f7c0d261603e820213624637d79923e16b147ece3826cd6d98ae042dd23ac2` |
| `LuaJIT/src/lj_load.c` | LuaJIT 2.1.ROLLING (upstream commit unrecorded) | `5cad42007acdaa25f20a57caa666302c1c6301c6dd4f4e3f4661eeafa784279a` |
| `LuaJIT/src/lj_gdbjit.c` | LuaJIT 2.1.ROLLING (upstream commit unrecorded) | `c0c7430331b187b759b275b8807d3503db3741ee5cb6dcbac2db0db33185176b` |
| `lua/src/lapi.c` | Lua 5.4.9 | `cd369dc6900a7696ca55ccbd4f50eadfa799b975f34b7afe450e1b859517a56e` |
| `lua/src/lundump.c` | Lua 5.4.9 | `7f7b078026e162161d402feba73a69cbe66a98558ec0745a445a595f42eac688` |

## Build integration retained

TrussC does not enable `LUAJIT_USE_GDBJIT`. Its luajit-cmake integration now
excludes `lj_gdbjit.c` from ordinary builds and rejects the GDB JIT CMake
switches. The optional upstream amalgamation still includes its disabled body;
no TrussC build defines the enabling macro. Custom compiler flags enabling it
are unsupported.

Other existing build/configuration support remains: MSVC `/bigobj`, the iOS
`LUA_USE_IOS` definition (upstream `loslib.c` stubs `system()`), and Web C++
exception recovery for owned load/lint boundaries. The separate upstream Lua
CLI `lua.c` remains excluded from this embedded library. The supplied Decision
calls the retained work “L4” without identifying a file or patch; no other
existing vendor changes were reverted or rewritten.

## Text loading policy (addon code, not vendor source)

TrussC's `runFile`, initial sketch/file loads, reload and lint use mode `"t"`;
initial file loads and reload share `runFile`. The live editor compiles source
as text. Binary chunks are rejected at these entry points. LuaJIT's generic
wrong-mode diagnostic is clarified in the owned file/lint paths to name binary
input and text mode. The script-visible standard `load`, `loadstring`
(when present), `loadfile`, `dofile` and `require` remain upstream, including
binary loading. Scripts choose and are responsible for inputs they load
through those functions. Native engine and sol2 C++ APIs are also unchanged.

Run `tests/engine` for both engines and the full addon tests when updating.
Tests for the removed reader guards were removed; text-loading and independent
lint lifetime/failure regressions remain.
