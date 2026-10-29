# tcxLua tests

Headless console test (no window). Lua files are written to a temp directory at
runtime.

XML bindings: empty-text serialization, escaped text and attribute round trips
through `Xml` and `parseXml` using the vendored pugixml.

Error-contained entry points (`tcxLua::call` / `tcxLua::runFile`):

- a syntax error, a missing file and a top-level `error()` in `runFile`, and a
  runtime `error()`, an index of nil, a nil function, a non-function value and
  `string.char(340)` in `call`, each return `false` without throwing; the state
  stays usable afterwards;
- syntax-error messages contain the file name; missing/unreadable files log an
  error, while empty files and files under Japanese paths load successfully;
- a valid file and a valid call return `true`, and call arguments arrive;
- repeated failing calls each log an error;
- the bundled `sketch.lua` accepts ordinary keys and ignores Shift / arrow keys;
- sol2's default script error handler still throws `sol::error`.

Lua `Node` surface (`TC_LUA_SKIP`):

- `Node.callAfterAsync`, `Node.callEveryAsync`, `Node.cancelAsyncTimer` and
  `Node.cancelAllAsyncTimers` are nil;
- `Node().callAfter`, `Node().callEvery` and `Node().cancelTimer` are still
  there.

The `Node` checks read the generated bindings (`src/generated/`), so they pass
once the bindings are regenerated from a `reference-data.json` that carries
`lua_skip` (see `tools/luagen-types/README.md`).

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR; a non-zero exit fails the job. Run it locally with:

```bash
trusscli run -p .          # from this directory
```

The harness also runs the engine-only regressions in `engine/`.
They generate their inputs locally and need no display. `luaLoadBounds`
checks large streamed text, finalizers and lint diagnostics, suggestion,
depth and failure handling. `luaTextOnly` checks binary rejection with clear
errors for each installed loader, including reader functions and requested
binary modes, and successful text loading. With debug open, it attempts loader
recovery through `debug.getupvalue` and audits globals, package tables, registry
and closure upvalues for the original loaders. Reader callbacks, explicit
(including nil) environments and PUC dofile continuations are also covered.
LuaJIT coverage includes non-consuming string.buffer sources for both `load`
and `loadstring`, explicit environments and binary rejection in every mode.
The full addon harness enables `LuaModulePreferences.debug` and `package`.
Both bundled engines are supported
(`-DUSE_LUAJIT=ON` selects LuaJIT).

The PUC targets `lintGlobalLifetime` and `lintScanLifetime` retain the lookup/GC
and throwing-container-move regressions. Configure with ASan flags to check
those lifetimes. Tests of the removed binary-buffer and reader patches were
removed with those patches.
