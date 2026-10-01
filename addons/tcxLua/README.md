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
