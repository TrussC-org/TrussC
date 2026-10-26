## License Note

Final result is combined to [LICENSE](./LICENSE).

- First author of tcxLua: `Copyright (c) 2026 Fumiya Funatsu` (**MIT License**, see https://github.com/TrussC-org/TrussC/blob/main/docs/LICENSE.md)
  - (and other collabolators may be added here also)
- Maintenance transferred to tettou771 in 2026
- Example base is from TrussC: `Copyright (c) 2024-2026 tettou771 <tettou771@gmail.com>` (**MIT License**)
- Lua: `Copyright (c) 1994–2025 Lua.org, PUC-Rio.` (**MIT License**, see https://www.lua.org/license.html)
- LuaJIT: `Copyright (c) 2005-2026 Mike Pall.` (**MIT License**, see https://github.com/LuaJIT/LuaJIT/blob/v2.1/COPYRIGHT)
- luajit-cmake: `Copyright (c) 2021 <zhaozg@gmail.com>` (**MIT License**, see https://github.com/zhaozg/luajit-cmake)

### Bundled engine patches (TrussC, 2026-10-21)

Lua `lapi.c` and LuaJIT `lj_load.c` use vendor-local `tcloadbuffer.h` to
buffer binary reader bytes without a size cap or allocator replacement. The
headers use each engine's internal allocation/free path: `luaM_realloc_` /
`luaM_free_` for PUC (including emergency GC retry and debt accounting), and
`lj_mem_realloc` / `lj_mem_free` for LuaJIT (native GC accounting and errors;
LuaJIT's native realloc path has no emergency GC retry).
Lua `lundump.c` checks remaining bytes, count products, debug relationships and
nesting before allocating/reading. LuaJIT `lj_bcread.c` checks byte/ULEB ranges,
prototype section products and debug ranges. Its reader intentionally does not
refill: `lua_loadx` supplies a stable complete binary block and the bundled
library loader supplies an already complete block. LuaJIT `lj_lex.c` accepts
complete binary blocks with size_t pointer ranges. Its package loader's exact
`~(size_t)0` first-piece size denotes in-process `luaJIT_BC_<name>` image data;
`tcloadbuffer.h` passes that piece through without buffering, and `lj_lex.c`
establishes the native end marker before the finite-block exception. Ordinary
finite bytecode readers retain the complete-buffer range checks, leaving MSize representation
checks on each prototype rather than the total file. Keep these patches together
when updating either engine. No vendor source includes tcxLua addon headers.
These additions follow the engines' MIT license; original notices are retained.
