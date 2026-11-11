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

tcxLua::runFile(*lua, getDataPath("sketch.lua"));  // streamed load + protected call
tcxLua::call(*lua, "setup");                       // calls through sol::protected_function
tcxLua::call(*lua, "keyPressed", key);             // arguments are passed on
```

- `tcxLua::call(sol::state&, const char* fn, args...)` returns `false` when the
  call raised an error. When `fn` is nil (the script does not define it),
  nothing is called and nothing is logged; it also returns `false`.
- `tcxLua::runFile(sol::state&, const fs::path&)` returns `false` for a missing
  or unreadable file, a syntax error or an error while the file runs. Files are
  streamed through a native file handle, supporting Unicode paths on Windows,
  and Lua diagnostics include the UTF-8 file name. Local files have no size cap.
- Both also work on the Web. The owned loading and lint translation units
  enable exception recovery for C++ allocation failures.
- Text source modules have no load size cap. Allocation failures use
  Lua's own "not enough memory" error; loaders do not replace the allocator.
  `runFile` and lint compile in text-only mode (`"t"`). Binary chunks are
  rejected with the engine's error naming binary input and text mode.
  Initial sketch/file loads and hot reload use `runFile`; the live editor
  compiles text only. Scripts calling the standard `load`, `loadstring`
  (LuaJIT), `loadfile`, `dofile`, or `require` get standard Lua behavior,
  including binary loading where the engine supports it. These functions are
  left upstream. Direct C++ sol2/native engine APIs also retain their standard
  behavior. Script authors are responsible for inputs they load themselves.
- Lint traversal uses the engine's `LUAI_MAXCCALLS` nesting limit (200 by
  default), covering the prototypes its source parser produces.
  Name and diagnostic storage grows with input; compilation has no allocation
  quota. Suggestions use constant scratch storage per comparison. Candidate
  visits cost O(undefined reads × candidates), including names outside the length
  window. A structure, allocation, depth or lookup
  failure returns one diagnostic for that file, preserving other files' results.
  Shared global-name snapshot failures return a `[lint]` diagnostic. If the
  diagnostic cannot be allocated, an Error goes to stderr and the result is
  empty. LuaJIT has no undefined-global scan; its lint path compiles each file.

日本語: TrussC はスクリプトファイルとソースをテキストとして読み込みます。
`runFile`、初回のスケッチ読み込み、リロード、lint ではモード `"t"` を使い、
バイナリチャンクを明確なエラーで拒否します。スクリプト自身が標準の
`load`、`loadstring`（LuaJIT）、`loadfile`、`dofile`、`require` を呼ぶ場合は、
バイナリ読み込みを含め、各 Lua エンジンの標準動作になります。

한국어: TrussC는 스크립트 파일과 소스를 텍스트로 읽습니다.
`runFile`, 최초 스케치 로드, 다시 로드, lint는 `"t"` 모드를 사용하며
바이너리 청크를 명확한 오류와 함께 거부합니다. 스크립트가 직접 표준
`load`, `loadstring`(LuaJIT), `loadfile`, `dofile`, `require`를 호출하면
바이너리 로드를 포함한 각 Lua 엔진의 표준 동작이 적용됩니다.

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

- Generated bindings expand trailing default arguments, so `clear(0.08)` and
  `setColor(1.0, 0.0, 0.0)` work. Handwritten bindings expose only their registered
  overloads; new members and default arguments need separate updates.
- Namespaced APIs and generic `Event<T>:listen` are not bound. Subscribe to
  events such as console and device disconnects in the C++ host.
- `end()` methods are replaced to `end_fbo()`, `end_shader()`, `end_cam()` etc ([Issue#11](https://github.com/ffunatsu/tcxLua/issues/11)).

## Migrating from v0.7.7 / v0.8.0

Since v0.7.7, 20 generated Lua names were removed (listed below); since v0.8.0,
none were removed. Two handwritten Texture methods were also removed since
v0.7.7, bringing the total to 22. Replacements marked C++ host are not Lua calls.
See also the [C++ v0.8 migration guide](../../docs/MIGRATION_0_8.md).

| Removed name(s) since v0.7.7 | Replacement / action |
| --- | --- |
| `Window.dispatchKeyPressToTree`, `Window.dispatchKeyReleaseToTree`, `Window.dispatchMousePressToTree`, `Window.dispatchMouseReleaseToTree`, `Window.dispatchMouseScrollToTree` (5) | Remove calls; input dispatch is internal, with no public replacement. |
| `Window.drawTreeNow`, `Window.syncRootSize`, `Window.tickTree` (3) | Remove manual tree work; the runtime handles it. |
| `Window.setApp` | Use `window->setApp<Scene>()` in the C++ host; the template has no Lua binding. |
| `WindowSettings.clipboardSize`, `WindowSettings.setClipboardSize` (2) | Remove the limit settings; clipboard storage grows with its content. |
| `WindowSettings.reserveUniformBuffer`, `WindowSettings.uniformBufferReserve` (2) | Move to `RuntimeSettings` and pass it to `runApp` in the C++ host. |
| `CoreEvents.setup` | Remove the call; initialization belongs to the runtime. |
| `CoreEvents.console` | Subscribe to `runtime().events().console` and call `tc::console::start()` in the C++ host. |
| `SerialDisconnectEventArgs`, `SerialDisconnectEventArgs.reason` (2) | Use `DeviceDisconnectEventArgs` and its `reason` field in the C++ host's disconnect listener. |
| `SerialDisconnectEventArgs.portName`, `.baudRate`, `.wasClean` (3) | Keep port/baud information yourself; `wasClean` has no replacement (explicit close no longer notifies). |
| `Texture.uploadCubemapFace`, `Texture.uploadCubemapMip` (handwritten, 2) | Use `Texture::loadCubemapData` in the C++ host; its pointer-array input has no Lua adapter. |

Names can remain while their behavior or arguments change:

| Change | Lua migration |
| --- | --- |
| Save/record functions return `SaveResult` | Use `if result:ok() then` (e.g. `if saveJson(j, path):ok() then`); userdata is truthy even on failure. |
| Angles use radians throughout | Convert degrees with `degrees * TAU / 360`, or use a `Deg` variant where available; FOV 45° is `TAU / 8`, and saved `Node.rotation` is radians too. |
| `VideoGrabber.setup` no longer accepts one argument | Supply width and height (`grabber:setup(width, height)`); no arguments reconnects at the last size. |
| `TcpServer.start` limits clients | The maximum defaults to 64 and must be positive; unlimited mode (`0`) is no longer supported. |
| `Fbo.begin` resets drawing state each time | Reapply matrix, camera, viewport and clip state after each begin; size queries refer to the active Fbo, scissor uses logical coordinates, and point size uses Fbo pixels. |
| `Fbo.readPixels` returns 8-bit RGBA | Even float Fbos read back as 0–255; ordinary Color components and hue remain 0–1. |
| Serial disconnect arguments and delivery | Handle `DeviceDisconnectEventArgs.reason` in the C++ host on the detecting read/write thread; `close`/`setup` do not notify, and `serial:setup()` reconnects. |
| Lua file loading is text-only | Ship text for `runFile`, initial loads, reloads, live editing and lint; standard Lua `load`/`loadfile`/`dofile`/`require` retain engine behavior. |
| `App.handleUpdate` changed after v0.8.0 | Replace `app:handleUpdate(width, height)` with `app:handleUpdate()`; headless updates already update children, Mods and timers. |
| App lifetime and event ordering | Review the C++ host: queued main-thread work runs between loop turns and is discarded when its App ends; closing the main window can leave other windows running. |

## Development

Local patches to both bundled Lua engines are recorded in [TRUSSC_MODIFICATIONS.md](TRUSSC_MODIFICATIONS.md).

### Binding generation

The current generators are [luagen](tools/luagen/README.md) (free functions) and
[luagen-types](tools/luagen-types/README.md) (types, enums and constants).
`tools/bindgen` is legacy. Regenerate from the repository root:

```bash
docs/reference/regenerate.sh
```

See the [reference pipeline](../../docs/reference/README.md) for prerequisites
and outputs. Build a tcxLua app, run [bindcheck](bindcheck/README.md), and run the
examples after regeneration. Json/Xml, SoundSource, Tween and the custom types
listed in luagen-types still have handwritten glue in `src/tcxLua.cpp`.

## License

[LICENSE](./LICENSE) is combined result. Please also check [docs/LICENSE_NOTE.md](docs/LICENSE_NOTE.md/) in detail.

C++ OOM recovery requires Release builds, or iterator debugging off (`_ITERATOR_DEBUG_LEVEL=0` on MSVC). With iterator debugging enabled, even empty string/container construction and moves can allocate a proxy on an error path.

App-owned registrations
-----------------------

Registrations created in an App constructor, callback or owned completion follow that App's lifetime, including lazy addon initialization. Replacing or removing the App retires its listeners and tools while other Apps keep running. Window teardown detaches the App, sends Window exit notifications around `App::exit()`, disconnects audio and waits for active audio callbacks, then cleans up the App before disposing native/GPU resources. Create window content with `window->setApp<Scene>()`; use `destroyApp()` to leave it empty.

Process lifecycle names are the same on App/Window events and
`runtime().events()`: `exitRequested`, `deviceLost`, `hotReloadUnload`.
Console moved from CoreEvents to `runtime().events().console`; the C++ host must
subscribe once and opt into stdin with `tc::console::start()`. Generic
`Event<T>:listen` is not registered for Lua.
Generated bindings include CloseRequestEventArgs and Window closeRequested;
code close skips that query. App ending retires owner-scoped subscriptions.
`Window::context()`, `dispatch*ToTree`, `tickTree`, `drawTreeNow` and
`syncRootSize` are internal and are not exposed to Lua.

Local script loading keeps Lua's protected call so memory errors return `false`.
It has no size notice or extra OOM retry/reserve machinery. Lint suggestions
have no per-file work budget.

ローカルスクリプトは Lua の保護された呼び出しで読み込み、メモリ不足時は
`false` を返します。サイズの Notice や OOM 専用の再試行・予備メモリはありません。
lint の候補探しにもファイルごとの作業量の上限はありません。

로컬 스크립트는 Lua의 보호된 호출로 읽으며 메모리가 부족하면 `false`를 반환합니다.
크기 Notice나 OOM 전용 재시도 및 예비 메모리는 없습니다.
lint 제안 검색에도 파일별 작업량 상한이 없습니다.
