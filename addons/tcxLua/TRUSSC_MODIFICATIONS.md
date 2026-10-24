# TrussC modifications

## LuaJIT

Upstream version: LuaJIT 2.1.ROLLING (`LuaJIT/src/luajit_rolling.h`).
Upstream commit: unknown; the import history and `docs/LICENSE.md` do not
record it. The MIT license is retained in `LuaJIT/COPYRIGHT`.

- `LuaJIT/src/lj_bcread.c`: adds checks so malformed bytecode cannot read
  beyond its section or allocate from unchecked counts. The byte/block and
  32/33-bit ULEB readers check bounds and overflow; table and prototype loading
  checks counts, size products and native representation limits before
  allocation. Debug loading checks line-info size and upvalue-name
  terminators. `lj_bcread` confines each prototype to its declared section.
  Refill logic is replaced by reads from a complete stable binary block,
  supplied by the load guard or the bundled-library loader. On update,
  recheck all bytecode accessors, allocation sizes and debug layouts, and
  retain section-local checks if upstream still lacks them.
- `LuaJIT/src/lj_gdbjit.c`: behaviour-preserving compiler compatibility patch.
  `#if` conditionals inside the `DSECT` macro arguments do not compile on
  MSVC (C2121). Paired `DSECT_BEGIN`/`DSECT_END` replace `DSECT`; all five
  invocations (CIE, FDE, info, line and its nested header) and the `#undef`s
  are updated in the EH-frame, debug-info and debug-line emission regions.
  The enabled GDB JIT preprocessed tokens are unchanged. On update, check
  whether upstream has removed directives from macro arguments; otherwise
  reapply the pairs and verify enabled GDB JIT compilation and token
  equivalence, including the target-dependent branches.
- `LuaJIT/src/lj_lex.c`: `lex_more` allows a complete first binary block at or
  above `LJ_MAX_BUF`, using pointer/`size_t` ranges. This preserves valid
  large-file loading with the new guard; text scratch buffers and individual
  prototypes retain native limits. The special `SIZE_MAX` embedded-bytecode
  end-marker convention is preserved. On update, recheck the first-block
  detection and end-marker path, and rerun large-file and embedded-module
  loading regressions.
- `LuaJIT/src/lj_load.c`: `lua_loadx` installs the vendor-local reader guard
  around the native protected parser and releases its buffer after lexer
  cleanup, on success or failure. This integration enables the range checks;
  it changes binary input buffering while source remains streamed. On update,
  recheck protected-call boundaries, cleanup ordering and all binary-loader
  entry points against `lj_bcread.c`'s stable-block requirement.
- `LuaJIT/src/tcloadbuffer.h` (added): `TcLuaLoadGuard` and
  `tcLuaLoadBegin`/`tcLuaLoadRead`/`tcLuaLoadEnd` collect finite binary-enabled
  reader input into one stable block, checking `size_t` overflow and `GCSize`
  narrowing. Native `lj_mem_realloc`/`lj_mem_free` preserve allocator ownership,
  GC accounting and memory errors; there is no load-size policy cap or
  allocator swap. Source, text-only rejection and `SIZE_MAX` embedded modules
  pass through. On update, recheck allocator/accounting APIs, mode handling,
  embedded-module conventions and cleanup after failed reads or allocations.

## PUC Lua

Upstream version: Lua 5.4.9 (`lua/src/lua.h`). The MIT license is retained
in that header.

- `lua/src/lapi.c`: `lua_load` installs the vendor-local reader guard before
  `luaD_protectedparser` and frees its buffer afterward for either status.
  This enables pre-allocation range checks by changing binary input buffering;
  source remains streamed. On update, recheck parser protection and cleanup
  paths, including allocation errors, against the undumper's stable-block
  requirement.
- `lua/src/lundump.c`: adds remaining-byte checks before block reads and
  allocations for strings, code, constants, child prototypes, upvalues and
  debug tables. Constant tags, closure/upvalue counts, line-info counts,
  absolute-line positions and intervals, line-number overflow, local-variable
  ranges and upvalue-name counts are validated instead of trusting the dump
  or assertions. `LoadState`, `loadFunction` and `luaU_undump` track nesting
  against `LUAI_MAXCCALLS`, covering parser-produced prototypes while bounding
  native-stack recursion. On update, review the bytecode format, every
  allocation site, debug lookup assumptions and the parser's nesting limit;
  retain equivalent checks before publishing prototypes to lint or runtime.
- `lua/src/tcloadbuffer.h` (added): `TcLuaLoadGuard` and
  `tcLuaLoadBegin`/`tcLuaLoadRead`/`tcLuaLoadEnd` buffer finite binary-enabled
  reader input with `size_t` overflow checks. Native
  `luaM_realloc_`/`luaM_free_` retain GC accounting, emergency collection/retry
  and native memory errors, without a load-size policy cap or allocator swap.
  Source and text-only rejection pass through. On update, recheck native
  allocation and error APIs, mode handling and buffer release on all outcomes.

When updating either engine, rerun `tests/engine` checks for malformed and
chunked bytecode, text-only rejection, allocator failures and binary memory
accounting, plus the addon loading/lint regressions. For LuaJIT, also run the
embedded-module and optional large-bytecode targets. These patches check
bytecode structure; they are not an instruction verifier or execution sandbox.
