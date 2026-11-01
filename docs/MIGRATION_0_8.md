# Migrating to v0.8.0

After updating to v0.8.0, delete existing build folders (or run a clean build).

## Box2D ownership and contact delivery

World, Body, CircleBody, RectBody and PolyShape cannot be moved; construct them in place and store/move smart pointers when needed.

b2Body user data is reserved by TrussC; put application data in your Body subclass. Fixture user data is reserved too.

Begin/End now run after each managed sub-step in Box2D order; Stay runs once per
update. Raw `b2World::Step()` events wait for the next fresh-frame
`World::update()`. Move destroyed-side Exit cleanup before release; surviving
sides receive Exit with null counterparts. Listeners may destroy bodies or the
World: recheck borrowed payload pointers after release. See the
[addon event contract](../addons/tcxBox2d/README.md#collision-events).

Bodies owned by a TrussC Body or RigidBody2D are destroyed through TrussC (`Body::destroy()`, removing the Mod, `World::clear()`); don't call raw `b2World::DestroyBody`/`DestroyFixture` on them. Raw destroys of bodies you created yourself are tracked when the body still has fixtures (the destruction listener); removing a body's last fixture and then the body without an `update()` in between is not tracked.

Register borrowed destruction delegates through `World::setDestructionListener()`
to preserve the internal observer. `clear()` keeps the physics world and settings
and is immediately reusable; a second `setup()` replaces the physics world.

## Stdin commands need `console::start()`

Console input is now opt-in (breaking change, #689 and #756). Neither `runApp()` nor `runHeadlessApp()` starts a stdin reader automatically. `CoreEvents::console` / `events().console` has been removed. Subscribe once to
`runtime().events().console` and add `console::start()` in `setup()`:

```cpp
void MyApp::setup() {
    consoleListener_ = runtime().events().console.listen([this](ConsoleEventArgs& e) {
        handleCommand(e);
    });
    console::start();
}
```

Keep `consoleListener_` as an `EventListener` member. Runtime pumps commands once on the main thread, before the first draw, after
the first Window closes, with zero Windows and headless. Console has no App
fan-out; subscriptions still carry their App owner token and retire on App
ending. The service stops after App ending; no manual pump is needed.

`console::stop()` disables delivery and drains pending commands. `console::start()` drains pending commands before enabling delivery again. The first start creates the process's only detached stdin reader, which continues consuming and discarding lines while stopped and ends at EOF. Restarting delivery does not create a new reader or replay discarded lines. Apps that never opt in leave stdin untouched. Web support remains unavailable.

## The MCP port line is only on the Logger

The raw stderr copy of the MCP port line is gone (breaking change, #414). After the port is bound, the server logs `[MCP] HTTP server listening on http://HOST:PORT/mcp` as a Logger Notice only: on stdout, in the file set with `TRUSSC_LOG_FILE`, and to `onLog` listeners. Logger lines carry a timestamp and a `[NOTICE]` prefix.

If a tool reads the port from stderr, change it to one of these:

- Read the Logger line from stdout or the log file, and match `HTTP server listening on http://` anywhere in the line. A console level of Warning or higher hides the line on stdout; the log file still gets it as long as its own level allows Notice.
- Set a fixed port with `TRUSSC_MCP_PORT=8080` and connect to `http://127.0.0.1:8080/mcp`. If that port is already in use, the server fails to start.

## OscReceiver::setup() owns its port

`OscReceiver::setup(port)` now binds the port exclusively (behavior change, #400). Before, it always turned on address and port reuse, so a second receiver on the same port, for example a second copy of the app, also bound without error, and unicast messages reached only one of them. Now the second `setup()` returns `false` and reports the error through `onParseError` and the log. The same goes for core `UdpSocket` on Windows: `bind()` on a socket that did not call `setReuseAddress(true)` or `setReusePort(true)` now sets `SO_EXCLUSIVEADDRUSE`, so it owns its port the way it already did on macOS and Linux.

If several receivers on one PC share a multicast port, replace `setup()` followed by `joinMulticast()` with one call:

```cpp
osc.setupMulticast(9000, "239.0.0.1");   // reuse, bind and join in one call
```

`joinMulticast()` / `leaveMulticast()` still work after `setup()`, but that port is not shared. Unicast apps need no change; check the return value of `setup()` to notice a port that is already in use.

## Every angle is in radians; degrees use `Deg` names

The last APIs that took degrees now take radians (breaking change, #236). The function names stay the same, so old code still compiles but passes degrees where radians are expected. Switch to the `Deg` name to keep degrees, or convert the value:

| Before (degrees) | Now (radians) | Keeps degrees |
| --- | --- | --- |
| `setupScreenPerspective(45)` | `setupScreenPerspective(TAU / 8)` (default) | `setupScreenPerspectiveDeg(45)` |
| `setupScreenFov(fov)` | `setupScreenFov(fov)` | `setupScreenFovDeg(fov)` |
| `setDefaultScreenFov(fov)` / `getDefaultScreenFov()` | same names | `setDefaultScreenFovDeg()` / `getDefaultScreenFovDeg()` |
| elliptical `path.arc(c, rx, ry, 0, 270)` | `path.arc(c, rx, ry, 0, TAU * 0.75f)` | `path.arcDeg(c, rx, ry, 0, 270)` |
| Node reflected `rotation` (JSON, MCP, inspector) | `rotation` in radians | `rotationDeg` (derived, not saved) |

`setupScreenFovWithSize()` / `setupScreenFovWithSizeDeg()` are new public functions (radians / degrees) for an explicit viewport size.

The screen FOV functions accept `0` (orthographic) or `0 < fov < TAU/2` (`0 < fov < 180` for the `Deg` names). Any other value, such as 60 left over from the degree version, logs one warning and is ignored, so the view stays as it was. Zero degrees also selects orthographic projection. `setupScreenOrtho()` and `setDefaultScreenOrtho()` are the readable spellings of `setupScreenFov(0)` and `setDefaultScreenFov(0)`; `getDefaultScreenFov()` returns `0` after either default setter.

Saved JSON: `rotation` is now read as radians, so a file saved before v0.8.0 (degrees under `rotation`) loads with wrong rotations. Convert those values with `deg2rad()`, or rename the key to `rotationDeg` in the file, which is still read and converted. New files save only `rotation`, in radians.

Not angles, unchanged: hue (`ColorHSB::h`, `ColorOKLCH::H`, 0–1 of a turn) and `tcx::geo::LatLon` (geographic degrees).

## Saving and recording return `SaveResult`

The save and record functions now report what went wrong instead of a plain `bool` or nothing (breaking change, #761 PR-B, #445, #440). `SaveResult` has `ok()`, an `error` (`AccessDenied`, `NoSpace`, `WriteFailed`, `Unknown`) and a `message`. It converts to `bool` explicitly, so `if (img.save("out.png"))` keeps working. Code that stores the result in a `bool`, passes it on as a `bool`, or returns it from a function that returns `bool` needs `.ok()`:

```cpp
bool saved = img.save("out.png").ok();
auto result = saveJson(json, "settings.json");
if (!result) logError() << result.message;
```

`bool` → `SaveResult`:
- `Pixels::save`, `Image::save`, `Fbo::save`, and `HasTexture::save` (an override in your own class must return `SaveResult` too)
- `saveJson`, `saveTextFile`, `appendToFile`, `FileWriter::open`, `Xml::save`
- `setLogFile` (and `Logger::setLogFile`, deprecated `tcSetLogFile`)
- `AudioRecorder::start`, `VideoWriter::open` / `addFrame` / `addFrameAt` / `writeAudio` / `submitFrame`, `ScreenRecorder::start`, `startRecording`

`void` → `SaveResult`: `AudioRecorder::stop()`, `VideoWriter::close()`, `ScreenRecorder::stop()`, `stopRecording()`. Ignoring the result still compiles, but finalizing a recording can fail (disk full, encoder error), and only this result tells you.

In Lua, test `result:ok()`: the result object itself is always truthy.

What else changes:
- `Pixels` / `Image` / `Fbo::save`, `saveJson`, `saveTextFile` and `Xml::save` write to `<file>.tmp` and then replace the file, so a failed save leaves the previous file as it was.
- `appendToFile`, `FileWriter`, the log file and the recorders write in place, and check every write.
- `saveScreenshot()` still returns `bool`, meaning "queued". A failed write later logs one Error. On Web it is still a browser download.
- `AudioRecorder` ends the take at the first failed write; `getRecordedSeconds()` counts what was actually written.
- `VideoWriter::getFrameCount()` counts frames the encoder accepted; a backend that stops accepting data ends the take, including macOS audio input that stays "not ready" for 5 s (previously that chunk was dropped while video recording continued). A wrong-size frame or other caller mistake fails that call only; the take goes on.

## OBJ and glTF loading return `LoadResult`

`tcx::obj::ObjLoader::load()` and `tcx::gltf::GltfModel::load()` now return `LoadResult` instead of `bool` (#761 PR-C). Missing model files return `FileNotFound` with the shared `file not found: <resolved path>` Error; rejected model data returns `DecodeFailed` with one Error. Relative paths resolve through `getDataPath()`. Neither loader gains `getPath()`.

`if (model.load(path))` keeps working. Code that assigns, passes or returns the result as a `bool` needs `.ok()` or an explicit cast:

```cpp
bool loaded = model.load("model.gltf").ok();
auto result = model.load("model.gltf");
if (!result) { /* inspect result.error and result.message; already logged */ }
```

`GltfModel::load()` now takes `const fs::path&`; a `std::string` still converts. For UTF-8 text on Windows, pass `utf8ToPath(text)` so it becomes a native filesystem path. External buffer and image URIs now decode percent escapes, such as `a%20b.png` for `a b.png`. `ObjExporter` changes too; see the next section.

## OBJ export returns `SaveResult`

`tcx::obj::ObjExporter::save()` now returns `SaveResult` (breaking change, #777). `if (exporter.save("model.obj"))` still works. Use `.ok()` when storing or passing the result as a `bool`, or keep the result to inspect the failure:

```cpp
bool saved = exporter.save("model.obj").ok();
auto result = exporter.save("model.obj");
if (!result) {
    // Inspect result.error and result.message; save() already logged one Error.
}
```

An empty exporter or a failed texture save now fails the call with one Error. OBJ and MTL text use the shared atomic save helpers; a failed MTL write leaves both previous text files intact. Replacement is atomic per file, not across the entire export: a final OBJ close or rename failure can leave the updated MTL alongside the previous OBJ, and textures already saved are kept.

## TrussC.h no longer brings in the HTTP library's system headers

`TrussC.h` used to include the MCP server's HTTP library, and with it a number of system headers. It no longer does (#789), which makes every file that includes `TrussC.h` compile faster. If your code uses one of these without including it itself, add the include:

- `<regex>` (`std::regex`, `std::regex_match`, ...)
- `<sys/stat.h>` (`stat`, `chmod`, `mkdir`) on macOS / Linux

Socket headers still come in through TrussC's network headers. The common standard headers (`<mutex>`, `<thread>`, `<map>`, `<fstream>`, `<sstream>`, `<atomic>`, `<functional>`, `<chrono>`, ...) also still come in through `TrussC.h`.

## Depth camera backends return an owned Device

The `tcx::depthcamera::DepthCamera` backend interface changes in v0.8 (#796,
PR-F2). Replace the handle's `bool openDevice()`, `closeDevice()` and
`captureInto()` overrides with `std::shared_ptr<Device> openDevice()`. Put the
source's resources and capture implementation in a `DepthCamera::Device`
subclass:

```cpp
struct SensorDevice : DepthCamera::Device {
    // Own the SDK session and all data capture touches here.
    StreamFreshness captureInto(DepthFrame& dst) override;
    ~SensorDevice() override; // release the SDK session here
};

std::shared_ptr<DepthCamera::Device> MyCamera::openDevice() {
    return std::make_shared<SensorDevice>(); // return nullptr if opening fails
}
```

Copy configuration from the backend into the Device at open. A Device must not
retain the backend's address or call its members. `captureInto()` still fills a
complete frame and returns `StreamFreshness`; only its owner changes. The capture
run holds the Device until capture ends. `close()` waits from another thread;
when called from capture itself it requests stop and returns, and the worker
releases the Device on exit. Backend destructors no longer need to call
`close()` before their members are destroyed. `setup()` closes the previous run
before opening a new Device.

Playback extensions move `readExtraBlock()` / `decodesBlockType()` overrides into
a `PlaybackDepthCamera::PlaybackDevice` subclass and override the protected
`makePlaybackDevice()` factory to return it. Keep custom decoding data in that
Device or in shared storage it owns. `setLoop()` updates the active Device's
atomic flag, so it can still change looping during threaded playback.

External backends (`tcxOrbbec`, `tcxAzureKinect`, `tcxRealSense`) need the same
migration in their own repositories; the old capture-on-backend hooks are removed.

## Fbo drawing has its own matrix and camera scopes (#821, PR 2)

Each successful `Fbo::begin()` starts an identity model, empty matrix/camera/scissor stacks,
full attachment viewport and a TAU/8 camera in attachment pixels. LOAD preserves pixels,
not drawing state. `end()` restores the suspended camera (including its CameraContext),
model, viewport, clipping and actual selected pipeline. Unmatched offscreen pushes are
warned about and discarded. Nested Fbos remain unsupported until PR 3.

Move transforms and their matching pops inside the target that draws them:

```cpp
// Before: relied on the outer transform leaking into offscreen PBR drawing.
pushMatrix(); translate(20, 10);
fbo.begin(); mesh.draw(); fbo.end();
popMatrix();

// After: keep each transform scope on its drawing target.
fbo.begin();
pushMatrix(); translate(20, 10); mesh.draw(); popMatrix();
fbo.end();
pushMatrix(); translate(100, 40); fbo.draw(0, 0); popMatrix();
```

Balance matrix, scissor and EasyCam begin/end on the same target. Set an offscreen clip
explicitly after begin. Screen pushes cannot clip or be popped inside an Fbo.
`setMatrix(model)` now consistently replaces only M and uploads V*M; remove any view
matrix previously baked into its argument. Copy screen matrices before begin if needed.

EasyCam uses the active target aspect and restores its entry camera on end. Remove
window-aspect compensation for Fbo drawing. `setDefaultScreenFov` and `pixelPerfect`
only configure screen defaults; call explicit screen setup inside each Fbo activation
for another camera. Explicit projection extents remain distinct from attachment size.

Use `tc::getWidth()`, `tc::getHeight()` and `tc::getSize()` in offscreen drawing code.
`App`/`RectNode` size getters still describe object geometry; window-named and framebuffer
getters still describe the host window. `getViewMatrix()` and `getProjectionMatrix()`
return active-target values. Bitmap/loaded text and RectNode clipping use target scale;
a font rasterized at host DPI 2 retains its requested unit size in a pixel-coordinate Fbo.

`setCameraPosition` is target-local and returns to its previous value on camera/target
exit. Its zero/manual default and window light/material/environment registration remain.
Pickability flags are unchanged: default Fbo cameras are non-pickable, explicit setup
and EasyCam remain pickable. Offscreen picking policy is still an open question.

Logical blend/depth flags retain continuity, separately from scoped pipeline selection.
The existing screen blend-after-Fbo behavior, offscreen premult handling and GPU point
transform/size corrections remain for PR 4. Repeating `setBlendMode` explicitly selects
that blend pipeline, even when the logical value already matches.

## Default listener priority on App events is now 0

On the events an App also receives (update, draw, exit, key, mouse, touch,
windowResized and so on), `listen()` without a priority now registers at
`EventPriority::BeforeApp` (`0`) instead of `100`. Other events still default
to `100`.

- A default listener ties with an explicit `0` and runs in registration order.
  A default `a` registered before an explicit-0 `b` now runs `a`, then `b`
  (before: `b`, then `a`).
- Default listeners now run before explicit `1`–`99` (before: after them) and
  before explicit `100` (before: same priority, mixed by registration order).
- So a default listener that sets `e.consumed = true` now stops even an
  explicit-100 listener that was registered earlier.

Check listeners whose order matters, and give them an explicit priority:

```cpp
overlayListener = events().mousePressed.listen([](MouseEventArgs& e) { /* ... */ }, 50);
pickListener    = events().mousePressed.listen([](MouseEventArgs& e) { /* ... */ });
// Before (v0.7): pickListener had priority 100, so it ran after overlayListener.
// After (v0.8):  pickListener has priority 0, so it now runs first.

// To keep it after overlayListener, give it a priority above 50:
pickListener    = events().mousePressed.listen([](MouseEventArgs& e) { /* ... */ }, 60);
```

## Process events and closing a Window

App code uses `events()`; non-App code (addons, app-wide handling) uses
`runtime().events()`. `exitRequested`, `deviceLost`, and `hotReloadUnload` retain
the same names on both hubs. Runtime receives them first, then attached Apps in
reverse attachment order (hot reload targets only replacing Apps). Quit and OS
close give every listener fresh `cancel=false`, OR all vetoes, and count a throwing listener as a veto. Only process quit retains the first nonempty veto reason. Device loss keeps
its mutable cancellation behavior.

`App::exit()` also runs on replacement, removal and hot reload. Use
`Window::setQuitOnClose(true)` for a window-close policy; it requests process
quit before closing anything. A Window exit listener runs during ending and
cannot provide that before-close veto guarantee.

Construct Apps with `Window::setApp<T>(args...)`, `runApp<T>()` or
`runHeadlessApp<T>()`; replace direct construction or attachment of an existing
shared App. Runtime retains open Windows and their App attachments. Dropping a
Window handle does not close it; call `close()` explicitly.

Root App setup-time quit is queried before audio subscription and the next
process services. Acceptance runs normal ending and prevents later console,
update and draw callbacks; a veto continues normally. A child App's request
waits until the current tree traversal unwinds, before the next process services.

At OS session end, an idle main thread allows the process veto query, then runs
normal ending on commit with one shared logout timeout (3 seconds by default).
A busy main thread accepts the query and performs no App work on commit: no
exit events, `App::exit()`, audio detach, cleanup or watchdog. Save promptly
during normal ending; busy termination cannot save App state.

Hot reload supports one Window and replaces the launch Window's App at the
main-thread outer-turn safe point. Opening a second or later Window while a
guest is loaded logs a Warning each time; behaviour is not guaranteed. Stop
threads, timers and async work in `events().hotReloadUnload`; reload does not
wait for or exclude that work. Successfully loaded modules stay mapped until
process exit, so repeated reloads can grow process memory.

TrussC no longer installs a built-in crash handler or writes crash reports; use
a platform debugger or external crash reporting if your application needs it.

### OS close and process veto

OS close first queries that Window's `closeRequested`. If accepted and the Window is the last open one or has `setQuitOnClose(true)`, Runtime queries `exitRequested` before closing anything. Either veto leaves every Window open. Both accepted use common process ending. Non-last ordinary OS close asks only `closeRequested`; `exitApp()` / Cmd+Q ask only `exitRequested`. Code `Window::close()` skips `closeRequested`; last-window/quit-on-close asks `exitRequested` before closing anything, and any veto keeps every Window open. A non-last plain code close asks no request events; normal Window exit notification and App ending still run. `CloseRequestEventArgs` contains only `cancel`; remove uses of its former `reason` field. `ExitRequestEventArgs::reason` remains the optional Windows shutdown-block text.

```cpp
class Document : public tc::App {
    tc::EventListener quit;
    bool unsaved = true;
    void setup() override {
        quit = events().exitRequested.listen([this](tc::ExitRequestEventArgs& e) {
            e.cancel = unsaved; // also keeps the last Window open on ×
        });
    }
};
```

## Window tree-dispatch methods are internal (v0.8.0)

`Window::context()`, `dispatchMousePressToTree`, `dispatchMouseReleaseToTree`,
`dispatchMouseMoveToTree`, `dispatchMouseScrollToTree`, `dispatchKeyPressToTree`,
`dispatchKeyReleaseToTree`, `tickTree`, `drawTreeNow` and `syncRootSize` are internal
in v0.8.0 and are no longer public C++ or Lua API. There is no public replacement
yet; the synthetic-input design remains open.

## Window and runtime settings, cursor and MCP window ids (v0.8.0)

- Move `WindowSettings::uniformBufferReserve` / `reserveUniformBuffer()` to `RuntimeSettings` and call `runApp<T>(windowSettings, runtimeSettings)`. The one-argument `runApp<T>(windowSettings)` uses default runtime settings.
- `WindowSettings::clipboardSize` / `setClipboardSize()` are removed. Clipboard text is no longer truncated at 65,536 bytes. Dropped file counts and UTF-8 paths no longer have the 16-file / 2048-byte limits. Storage grows with the actual content; allocation and platform limits still apply. The low-level `sapp_desc::clipboard_size`, `max_dropped_files` and `max_dropped_file_path_length` fields are removed; remove assignments to these fields.
- Cursor shape, visibility and custom images are app-wide, shown in whichever window the pointer is over. ImGui contexts change it only while their own window is under the pointer.
- `pixelPerfect` is process-wide in v0.8.0: the launch window's setting applies to every window.
- `tc_list_windows` returns each window's positive native identity as `id` instead of `index`. Pass that `id` as other tools' `window` argument; resource list indices are unchanged.
- `exitApp()` now goes through `exitRequested`, whose listeners can veto it; before 0.8 it could not be cancelled. A “show a confirm dialog, then call `exitApp()`” pattern receives `exitRequested` again on that call, so remember acceptance in a flag, as `quitConfirmed_` does in [exitExample](../examples/events/exitExample/src/tcApp.cpp).
