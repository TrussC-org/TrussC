# tcxLua

- Using Lua 5.4 sources (NOTE: Lua 5.5 is currently not supported by Sol2)
- Using [sol2](https://github.com/ThePhD/sol2).
- Versions of Lua, sol2, LuaJIT and luajit-cmake: [docs/LICENSE.md](../../docs/LICENSE.md#third-party-libraries).
- LuaJIT can be enabled with `-DUSE_LUAJIT=ON` in cmake (using [luajit-cmake](https://github.com/zhaozg/luajit-cmake), disabled by default).

## Binding coverage

### Dones

- trussc (TrussC.h directly defined functions)
- cmath (common use ones only), tcMath
- tcPrimitives.h, tcLog.h
- Vec2, Vec3, IVec2, IVec3, Vec4, Mat4, Mat3, Quaternion, Rect
- Color, colors (constants)
- Mesh, Shader
- Fbo, Texture, Image, Pixels
- EasyCam, Light, Material
- Font, Path
- Json, Xml
- `Tween<T>` (as TweenFloat, TweenVec2, TweenVec3, TweenColor)
- Sound, MicInput

### Sound lifetime

A `Sound` plays only while it, or a copy of it, is alive (the C++ rule). In
Lua the object goes away when the garbage collector collects it, at a time the
script does not control, so a script keeps a reference (a global, or a field
of a table it keeps) to every `Sound` it wants to hear:

```lua
bgm = Sound.new()          -- global: plays until the script drops it
bgm:load("music.ogg")
bgm:play()
```

A `Sound` held only in a local variable stops when the GC collects it.

## Error handling

Call Lua entry points through the two helpers. They do not throw: a Lua error
(syntax or runtime) is logged with `logError("tcxLua")`, the helper returns
`false`, and the app keeps running. In a hot-reload setup, fix the script and
load it again to recover.

```cpp
auto lua = tcxLua().getLuaState();

tcxLua::runFile(*lua, getDataPath("sketch.lua"));  // loadTextFile + safe_script with sol::script_pass_on_error
tcxLua::call(*lua, "setup");                       // calls through sol::protected_function
tcxLua::call(*lua, "keyPressed", key);             // arguments are passed on
```

- `tcxLua::call(sol::state&, const char* fn, args...)` returns `false` when the
  call raised an error. When `fn` is nil (the script does not define it),
  nothing is called and nothing is logged; it also returns `false`.
- `tcxLua::runFile(sol::state&, const fs::path&)` returns `false` for a missing
  or unreadable file, a syntax error or an error while the file runs. Files are
  read through `loadTextFile`, supporting Unicode paths on Windows, and Lua
  diagnostics include the UTF-8 file name.
- Both also work on the Web, where C++ exceptions cannot be caught.

sol2's own defaults are unchanged: on an error, `lua->safe_script(...)` and
`safe_script_file(...)` without `sol::script_pass_on_error` throw `sol::error`,
and so does a direct call such as `(*lua)["draw"]()` in Release builds. An
uncaught exception ends the app.

`exampleFileReload` uses the helpers. `exampleLiveUpdate` uses protected calls
and logs runtime errors once per compiled expression with `OnceGate`; it keeps
trying each frame, and editing the expression resets the log gate.

## Threads

Lua functions only run on the main thread. A `lua_State` must not be entered
from two threads at once, so callbacks that would run on another thread are not
exposed to Lua: `Node:callAfterAsync`, `callEveryAsync`, `cancelAsyncTimer` and
`cancelAllAsyncTimers` are not available. Use `callAfter` / `callEvery`, which
run in the update loop on the main thread.

In C++, these declarations carry `TC_LUA_SKIP` (`tcAnnotations.h`), and the
binding generator leaves them out.

## Lua module sandboxing

`tcxLua::getLuaState()` accepts a `LuaModulePreferences` struct that controls which
Lua standard libraries are loaded. The defaults are:

| Module | Default | Notes |
|--------|---------|-------|
| `base`, `math`, `string`, `table`, `coroutine` | **on** | Safe core set |
| `io`, `package` | **off** | Opening these is effectively granting **arbitrary code execution** to whatever Lua you load — `io.popen` runs shell commands, `package.loadlib` loads any native library |
| `os`, `debug`, `bit32`, `ffi`, `utf8` | **off** | Off for similar reasons (`os.execute`, `os.remove`, etc.) |

If your app only runs Lua scripts you authored and ship yourself, opting back in is fine:

```cpp
tcxLua lua;
tcxLua::LuaModulePreferences prefs;
prefs.io = true;
prefs.package = true;   // needed for `require` of external Lua modules
auto state = lua.getLuaState(prefs);
```

**Do NOT enable `io` / `package` if any of these are true:**

- The Lua source can be edited by the end-user (hot-reload from disk, in-app script editor)
- The app downloads, fetches, or otherwise loads Lua from untrusted sources
- Your app ships pre-compiled Lua that you cannot guarantee hasn't been tampered with

In those cases, enabling `io` / `package` lets the script `io.popen("rm -rf ~")` or
load arbitrary native code via `package.loadlib`. Stick with the default safe set.

See [issue #80](https://github.com/TrussC-org/TrussC/issues/80) for the
historical context — these were on by default until 2026-05-27 (PR #97).

## Known Issues

- Default value is not treated perfectly now ([Issue#1](https://github.com/ffunatsu/tcxLua/issues/1)). So you may need additional values when using methods for example:
  - `clear(0.08, 1.0)` instead of `clear(0.08)`
  - `setColor(1.0, 0.0, 0.0, 1.0)` instead of `setColor(1.0, 0.0, 0.0)`
- `end()` methods are replaced to `end_fbo()`, `end_shader()`, `end_cam()` etc ([Issue#11](https://github.com/ffunatsu/tcxLua/issues/11)).

## Development

### Bindgen

Please read [tools/bindgen/README.md](tools/bindgen/README.md) in detail.

```bash
$ cd tools/bingen
# $ pip install uv # only at first time
# $ uv sync # only at first time
$ uv run main.py ../../../../core/include/TrussC.h
$ cp trussc_generated.cpp ../../src/generated
```

## License

[LICENSE](./LICENSE) is combined result. Please also check [docs/LICENSE_NOTE.md](docs/LICENSE_NOTE.md/) in detail.
