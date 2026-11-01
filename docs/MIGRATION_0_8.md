# Migrating to v0.8.0

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

Console input is now opt-in (breaking change, #689 and #756). Neither `runApp()` nor `runHeadlessApp()` starts a stdin reader automatically. If your app subscribes to `events().console`, add `console::start()` in `setup()`:

```cpp
void MyApp::setup() {
    consoleListener_ = events().console.listen([this](ConsoleEventArgs& e) {
        handleCommand(e);
    });
    console::start();
}
```

Keep `consoleListener_` as an `EventListener` member. Both runners pump commands on the main thread and stop delivery before `exit()` / `cleanup()`; no manual pump is needed.

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

## No caps on image size, sound polyphony or OSC nesting

These limits are gone; if your app needs a cap, set it yourself.

- **Image:** the 64 MiB limit on encoded image input from memory (`Pixels::loadFromMemory`, `Image::loadFromMemory`, URL and network-buffer loads) is removed, as it never applied to local files. `LoadError::TooLarge` stays for its other reasons (GPU texture size, URL/network admission, platform size). Nothing to change.
- **Sound:** `AudioSettings::maxPolyphony` now defaults to `0`, which means no cap (it was `32`), and `AudioEngine::DEFAULT_MAX_PLAYING_SOUNDS` is `0`. The 33rd simultaneous `Sound::play()` used to return `false` and was counted in `droppedPolyphonyLimit`; it now plays. `AudioEngine::getMaxPolyphony()`, `AudioDeviceChangedArgs::maxPolyphony` and the `tc_get_audio_state` field report the cap, so they read `0` when none is set, not the number of slots. A positive value still works as a cap; negative values mean no cap, as `0` does. `PlayingSoundInfo::slot` is no longer bounded by a fixed range. A `SoundStream`'s own `loadStream(path, maxPolyphony)` default of `1` is unchanged.
- **OSC:** `OscBundle::MAX_NESTING_DEPTH` is removed, and bundles nest as deep as the 65,536-byte packet allows (the datagram bound stays). If you used the constant, delete the check or replace it with a limit of your own in an `onBundleReceived` handler. `OscBundle` copies are now shallow: nested bundles are shared between a bundle and its copies (they are never modified once added), so a copy costs one level, and the destructor walks the nesting without recursion. `OscBundle::Element` is now `std::variant<OscMessage, std::shared_ptr<OscBundle>>`.
