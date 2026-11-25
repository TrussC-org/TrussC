# Migrating to v0.9.0

For apps and addons written for v0.8, these are the source and behavior changes in v0.9.0. Examples use `using namespace tc;` and `using namespace std;`, with objects stored as `tcApp` members. The guide is English only, like [MIGRATION_0_8.md](MIGRATION_0_8.md).

## VideoPlayer and VideoGrabber getPixels()

`VideoPlayer::getPixels()` and `VideoGrabber::getPixels()` now return `const Pixels&` instead of a raw pointer, giving read-only access to frame dimensions, format and RGBA bytes (#1546, PR #1589).

Before (`video` is a `VideoPlayer` member; the same change applies to `VideoGrabber`):

```cpp
void tcApp::update() {
    const unsigned char* bytes = video.getPixels();
}
```

v0.9:

```cpp
void tcApp::update() {
    const unsigned char* bytes = video.getPixels().getData();
}
```

The returned storage belongs to the video object. For editable pixels, use `clone()` to copy it into an owned `Pixels` member:

```cpp
void tcApp::update() {
    pixels = video.getPixels().clone();
}
```

`VideoPlayer::getPixels()` and `getTexture()` may be empty during hardware decoding on any platform; pixel access does not trigger conversion or GPU readback. Use `video.draw()` for display. To obtain CPU pixels, allocate an `Fbo` member to the video's dimensions after loading, in `tcApp::setup()`:

```cpp
fbo.allocate(video.getWidth(), video.getHeight());
```

In `tcApp::update()`, after `video.update()` and only when `video.isFrameNew()`, draw into that Fbo in a scope:

```cpp
{ auto target = fbo.scoped();
    video.draw(0, 0);
}
```

After the scope ends, read into a `Pixels pixels` member and check success before using it:

```cpp
if (fbo.readPixels(pixels)) logNotice() << pixels.getWidth();
```

Fbo readback does not work on the Vulkan beta; see [MIGRATION_0.9.md › Vulkan backend (beta)](#vulkan-backend-beta).

## Window::getFps() and getTargetFps()

`Window::getFps()` now measures that window's update rate, matching global `getFps()`, so code that needs the configured drawing target must use `Window::getTargetFps()` (#1562, PR #1575).

Before (reading the configured target):

```cpp
void tcApp::update() {
    float target = getWindow()->getFps();
}
```

v0.9:

```cpp
void tcApp::update() {
    float target = getWindow()->getTargetFps();
}
```

The target retains `VSYNC` (-1), `EVENT_DRIVEN` (0), or a positive rate. Global `getFps()` and `getFpsSettings()` keep their meanings.

## MouseButton in events and mouse state

`MouseEventArgs::button`, `MouseDragEventArgs::button` and `getMouseButton()` now use `MouseButton` instead of integers, matching input injection (#1564, PR #1586).

Before:

```cpp
void tcApp::mousePressed(const MouseEventArgs& e) {
    if (e.button == 0) logNotice() << "Left button";
}
```

v0.9:

```cpp
void tcApp::mousePressed(const MouseEventArgs& e) {
    if (e.button == MouseButton::Left) logNotice() << "Left button";
}
```

Use `Left`, `Right`, `Middle` and `None`; integer comparisons no longer compile. The `MOUSE_BUTTON_*` constants remain until v1.0 as deprecated **MouseButton** aliases, so they no longer supply integers to other APIs. Use the enum directly for state checks:

```cpp
void tcApp::update() {
    if (getMouseButton() == MouseButton::None) logNotice() << "No button";
}
```

The deprecated short `(Vec2, int)` callbacks still take integers; prefer the rich event callback shown here. `MouseMoveEventArgs` has no button field.

## DragDropEventArgs::files and filesDropped()

Dropped file lists now use `vector<filesystem::path>`, matching file-open events and preserving native paths (#1565, PR #1579).

Before:

```cpp
void tcApp::filesDropped(const vector<string>& files) {
    for (const auto& file : files) logNotice() << file;
}
```

v0.9:

```cpp
void tcApp::filesDropped(const vector<filesystem::path>& files) {
    for (const auto& file : files) logNotice() << file.filename();
}
```

`DragDropEventArgs::files` and `InputEvent::filesDropped(paths, pos)` use the same path vector. For injection, replace an existing `vector<string>` with paths:

```cpp
void tcApp::setup() {
    getWindow()->injectInput(InputEvent::filesDropped(vector<filesystem::path>{"image.png"}, Vec2{0, 0}));
}
```

On Web, dropped entries are browser file names, not desktop paths; there is no API that turns those names into desktop paths. For portable display text, convert the path explicitly to UTF-8:

```cpp
void tcApp::filesDropped(const DragDropEventArgs& e) {
    for (const auto& file : e.files) logNotice() << pathToUtf8(file.filename());
}
```

## TouchEventArgs::touches

`TouchEventArgs::touches` is now a `vector<TouchPoint>` containing only the event's active entries, removing the fixed array's inactive slots and separate `numTouches` count (#1570, PR #1577).

Before:

```cpp
void tcApp::touchPressed(const TouchEventArgs& e) {
    for (int i = 0; i < e.numTouches; ++i) logNotice() << e.touches[i].id;
}
```

v0.9:

```cpp
void tcApp::touchPressed(const TouchEventArgs& e) {
    for (const auto& touch : e.touches) logNotice() << touch.id;
}
```

Use `e.touches.size()` for the count, including when constructing injected events; `numTouches` is removed. `MAX_TOUCHES` remains the backend bound, not the event's count. `x()`, `y()` and `id()` read the first entry, or return zero when empty.

## HapPlayer::getPixels()

`tcx::hap::HapPlayer::getPixels()` now always returns empty `Pixels`, keeping Hap frames compressed on the GPU instead of silently decoding a full RGBA frame in a getter (#1593, PR #1595).

Before (`hap` is a `tcx::hap::HapPlayer` member, with the tcxHap addon enabled):

```cpp
void tcApp::update() {
    const unsigned char* bytes = hap.getPixels();
}
```

v0.9: allocate an `Fbo fbo` member to the movie's dimensions after loading in `tcApp::setup()`:

```cpp
fbo.allocate(hap.getWidth(), hap.getHeight());
```

In `tcApp::update()`, after `hap.update()` and only when `hap.isFrameNew()`, render the frame into the Fbo:

```cpp
{ auto target = fbo.scoped();
    hap.draw(0, 0);
}
```

After the scope ends, read into a `Pixels pixels` member and check success:

```cpp
if (fbo.readPixels(pixels)) logNotice() << pixels.getWidth();
```

Fbo readback does not work on the Vulkan beta; see [MIGRATION_0.9.md › Vulkan backend (beta)](#vulkan-backend-beta). Details: [tcxHap README › Read Hap frames as pixels](../../addons/tcxHap/README.md#read-hap-frames-as-pixels).

## EasyCam mouse button setters

`EasyCam` now names its enum setters `setOrbitMouseButton()` and `setPanMouseButton()` to match `MouseButton` and their getters (PR #1603).

Before (`cam` is an `EasyCam` member):

```cpp
void tcApp::setup() {
    cam.setOrbitButton(MOUSE_BUTTON_RIGHT);
    cam.setPanButton(MOUSE_BUTTON_MIDDLE);
}
```

v0.9:

```cpp
void tcApp::setup() {
    cam.setOrbitMouseButton(MouseButton::Right);
    cam.setPanMouseButton(MouseButton::Middle);
}
```

The integer setters `setOrbitButton(int)` / `setPanButton(int)` and integer getters remain deprecated until v1.0; typed `MOUSE_BUTTON_*` aliases no longer fit those setters. For code written against the unreleased enum overloads, replace `setOrbitButton(MouseButton)` / `setPanButton(MouseButton)` with the new names shown here. Read bindings with `getOrbitMouseButton()` / `getPanMouseButton()`.

## Also changed

Additional breaks found in the merged v0.9 PRs are included in the migration examples:

- PR #1589: video pixels are read-only, including from non-const players and grabbers; use `clone()` into owned `Pixels` to edit ([VideoPlayer and VideoGrabber getPixels()](#videoplayer-and-videograbber-getpixels)).
- PR #1592: `VideoPlayer::getTexture()` may be empty during hardware decoding on any platform; use `draw()` into an Fbo for pixels ([VideoPlayer and VideoGrabber getPixels()](#videoplayer-and-videograbber-getpixels)).

## Vulkan backend (beta)

The v0.9 Vulkan backend is experimental on Linux (X11) and Windows (Win32) and supports one native window; additional `createWindow()` calls return `nullptr` with a warning. Use the launch window:

```cpp
void tcApp::setup() {
    getWindow()->setTitle("Vulkan single window");
}
```

Vulkan readback is not supported in v0.9: window capture (`grabScreen`, `saveScreenshot`, `saveScreenshotAsync`), Fbo readback (`readPixels`, `readPixelsFloat`, `copyTo` to an Image, `readStencil`), `StorageBuffer::read` and `readAsync`, MCP screenshot and window/Fbo image analysis, and `ScreenRecorder` / `VideoWriter` fed from a window or an Fbo fail with a warning. The full list is in [BUILD_SYSTEM.md › Experimental Vulkan build (Linux and Windows)](BUILD_SYSTEM.md#experimental-vulkan-build-linux-and-windows). There is no Vulkan API to enable readback. Asynchronous buffer reads deliver deferred failure; recorders stop at the first readback failure. To capture, rebuild with the platform's default backend (GL on Linux, D3D11 on Windows) by clearing the Vulkan override:

```bash
cmake --preset linux -DTC_GFX_BACKEND=
cmake --build --preset linux
```

```powershell
cmake --preset windows -DTC_GFX_BACKEND=
cmake --build --preset windows --config Debug
```

Backend selection is a build option, not a `tcApp` runtime setting. See [BUILD_SYSTEM.md › Experimental Vulkan build (Linux and Windows)](BUILD_SYSTEM.md#experimental-vulkan-build-linux-and-windows).
