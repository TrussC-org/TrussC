# Migrating to v0.8.0

This guide is for apps and addons written for v0.7.7. It lists what you may need to change, by area.

After updating, delete your build folders or do a clean build.

Each section starts with the changes that can break your code: it no longer compiles, or it compiles but behaves differently. Each has an example of the old and the new code. After the breaking changes come the new features you can adopt when you want.

**Contents**

- [App, window and runtime](#app-window-and-runtime): [Create Apps through a window or a launcher](#create-apps-through-a-window-or-a-launcher) · [The launch window is window #1](#the-launch-window-is-window-1) · [Closing the launch window no longer always quits](#closing-the-launch-window-no-longer-always-quits) · [exitApp() is the one way to quit](#exitapp-is-the-one-way-to-quit) · [Quitting can be canceled, and each window can refuse to close](#quitting-can-be-canceled-and-each-window-can-refuse-to-close) · [App callbacks are listeners now, and default listeners run first](#app-callbacks-are-listeners-now-and-default-listeners-run-first) · [Node and Mod setup() runs when they join the tree](#node-and-mod-setup-runs-when-they-join-the-tree) · [events() needs an App, and process events are on runtime()](#events-needs-an-app-and-process-events-are-on-runtime) · [Headless apps fire update and exit events](#headless-apps-fire-update-and-exit-events) · [A hidden window keeps updating but stops drawing](#a-hidden-window-keeps-updating-but-stops-drawing) · [Quitting during setup()](#quitting-during-setup) · [Logout, restart and shutdown](#logout-restart-and-shutdown) · [Hot reload supports one window](#hot-reload-supports-one-window) · [Stdin commands need console::start()](#stdin-commands-need-consolestart) · [Window and runtime settings](#window-and-runtime-settings) · [Internal Window functions are no longer public](#internal-window-functions-are-no-longer-public) · [No built-in crash handler](#no-built-in-crash-handler)
- [Async work and threads](#async-work-and-threads): [runOnMainThread() runs between turns, and drops work for an ended App](#runonmainthread-runs-between-turns-and-drops-work-for-an-ended-app) · [Thread::waitForThread() on its own thread](#threadwaitforthread-on-its-own-thread)
- [Rendering](#rendering): [Every angle is in radians; degrees use Deg names](#every-angle-is-in-radians-degrees-use-deg-names) · [Each Fbo has its own matrices, camera and clipping](#each-fbo-has-its-own-matrices-camera-and-clipping) · [Scissor rectangles use logical coordinates](#scissor-rectangles-use-logical-coordinates) · [grabScreen() reads the finished frame in afterFrame](#grabscreen-reads-the-finished-frame-in-afterframe) · [The swapchain pass functions are removed](#the-swapchain-pass-functions-are-removed) · [Cubemap textures are uploaded in one call](#cubemap-textures-are-uploaded-in-one-call) · [Shader subclasses use pipelineForCurrentTarget()](#shader-subclasses-use-pipelineforcurrenttarget) · [Fbo::readPixels() on float Fbos, and failed allocations](#fboreadpixels-on-float-fbos-and-failed-allocations)
- [Loading and saving](#loading-and-saving): [Saving and recording return SaveResult](#saving-and-recording-return-saveresult) · [OBJ and glTF loading return LoadResult](#obj-and-gltf-loading-return-loadresult) · [OBJ export returns SaveResult](#obj-export-returns-saveresult) · [LoadError has two new values](#loaderror-has-two-new-values) · [Sound files are found through getDataPath()](#sound-files-are-found-through-getdatapath)
- [Devices](#devices): [Serial](#serial) · [VideoGrabber](#videograbber) · [MidiIn and MidiOut (tcxMidi)](#midiin-and-midiout-tcxmidi) · [AudioEngine and MicInput](#audioengine-and-micinput) · [DepthCamera backends return an owned Device](#depthcamera-backends-return-an-owned-device) · [DepthCamera connection state](#depthcamera-connection-state)
- [Networking and addons](#networking-and-addons): [OscReceiver::setup() owns its port](#oscreceiversetup-owns-its-port) · [OSC and MIDI receive queues](#osc-and-midi-receive-queues) · [TcpServer limits clients to 64 by default](#tcpserver-limits-clients-to-64-by-default) · [tcxCurl follows redirects and limits response size](#tcxcurl-follows-redirects-and-limits-response-size) · [tcxWebSocket limits message size](#tcxwebsocket-limits-message-size) · [Box2D ownership and contact events](#box2d-ownership-and-contact-events)
- [Limits that are gone](#limits-that-are-gone)
- [Tools, build and MCP](#tools-build-and-mcp): [TrussC.h no longer brings in the HTTP library's system headers](#trussch-no-longer-brings-in-the-http-librarys-system-headers) · [MCP tools need a dev build or a grant](#mcp-tools-need-a-dev-build-or-a-grant) · [MCP window ids](#mcp-window-ids) · [The MCP port line is only on the Logger](#the-mcp-port-line-is-only-on-the-logger) · [Shader targets in custom CMake](#shader-targets-in-custom-cmake)
- [New in v0.8.0](#new-in-v080)

## App, window and runtime

v0.8.0 reorganizes how apps, windows and the runtime fit together. The **runtime** is the process-wide part of TrussC. It owns every window and every App. Each **window** shows one App. The launch window, the one `runApp()` opens, is now an ordinary window.

### Create Apps through a window or a launcher

You can no longer make an App yourself and hand it to a window. Create it with `Window::setApp<T>(args...)`, `runApp<T>()` or `runHeadlessApp<T>()`. Constructing an App any other way, for example with `make_shared<MyApp>()`, compiles but throws `std::logic_error` when it runs.

`Window::setApp(shared_ptr<App>)` is removed. To remove a window's App, call `destroyApp()` instead of `setApp(nullptr)`.

```cpp
// Before
win->setApp(make_shared<SubApp>(42));
win->setApp(nullptr);

// After
win->setApp<SubApp>(42);
win->destroyApp();
```

The runtime keeps every open window and its App. Dropping your `Window` handle does not close the window; call `close()` to close it.

### The launch window is window #1

The launch window is now a `Window` like any other. For the App in it, `getWindow()` returns that window. In v0.7.7 it returned `nullptr`, so code that used `nullptr` to recognize the main App no longer works.

```cpp
// Before: only the main App had no window
bool isMainApp = (getWindow() == nullptr);

// After: keep your own flag, or compare with the window you launched
```

You can call `close()`, `setQuitOnClose()` and the other `Window` functions on the launch window too.

### Closing the launch window no longer always quits

`quitOnClose` is `false` by default, also on the launch window. Clicking × on the launch window while another window is open now closes only that window, and the app keeps running. The app quits when the last window closes. To quit whenever the launch window closes, as in v0.7.7, turn on `quitOnClose`:

```cpp
// main.cpp
return runApp<tcApp>(WindowSettings().setSize(1280, 720).setQuitOnClose(true));
```

### exitApp() is the one way to quit

`exitApp(int code = 0)` is now the one way to quit, in windowed and headless apps alike. `App::requestExit()` and `requestExitApp()` still compile, with a deprecation warning, and now simply call `exitApp()`. In v0.7.7, `App::requestExit()` only set a flag that `runHeadlessApp()` read, so it did nothing in a windowed app.

```cpp
// Before
requestExit();   // or requestExitApp()

// After
exitApp();
exitApp(3);      // with an exit code
```

- `requestExit()` now asks `exitRequested`, so a listener can cancel it, as it already could for `requestExitApp()`. In a headless app, `requestExit()` used to stop the loop without asking.
- `requestExitApp()` used to keep the current exit code. It now sets the code to `0`, as `exitApp()` does.
- `App::isExitRequested()` is deprecated. It still returns `true` after `requestExit()` was called on that App, but TrussC no longer reads it. To react to a quit, use `exit()` or `events().exitRequested`.
- The exit reason `request-exit-app` is gone. `exitApp()` reports `exit-app`.

### Quitting can be canceled, and each window can refuse to close

- `exitApp()` now asks `exitRequested` first, so a listener can cancel it. In v0.7.7 it could not be canceled. The exit code you pass is kept even if a listener cancels the quit.
- A common pattern is "show a confirm dialog, then call `exitApp()`". That call asks `exitRequested` again, so remember that the user already said yes, as `quitConfirmed_` does in [exitExample](../examples/events/exitExample/src/tcApp.cpp).
- Every listener now gets its own `cancel = false` and the answers are combined: if any listener cancels, the quit is canceled. A listener can no longer undo an earlier listener's cancel by setting `cancel = false`. A listener that throws counts as a cancel. Only the quit request keeps the first non-empty `reason`.
- New: each window has `closeRequested` (`CloseRequestEventArgs`, one field, `cancel`). It is asked when the user closes that window from the OS, for example with the × button.

Which question is asked depends on how the window closes:

- **The user closes a window that is not the last one.** Only that window's `closeRequested` is asked.
- **The user closes the last window, or a window with `setQuitOnClose(true)`.** That window's `closeRequested` is asked first. If it is accepted, `exitRequested` is asked before anything closes. If either is canceled, every window stays open.
- **`exitApp()` or Cmd+Q.** Only `exitRequested` is asked.
- **Your code calls `Window::close()`.** `closeRequested` is not asked. If it is the last window or has `setQuitOnClose(true)`, `exitRequested` is asked once before anything closes, and a cancel leaves every window open. Otherwise nothing is asked, and the window's exit events and the App's ending still run.

To keep an unsaved document open, cancel in `exitRequested`. It also covers × on the last window:

```cpp
class Document : public App {
    EventListener quitListener;
    bool unsaved = true;
    void setup() override {
        quitListener = events().exitRequested.listen([this](ExitRequestEventArgs& e) {
            e.cancel = unsaved;   // also keeps the last window open on ×
        });
    }
};
```

`App::exit()` runs whenever an App ends: when its window closes, when it is replaced or removed, and on hot reload.

A window's `exit` listener runs while the window is already ending, so it cannot stop the close. Use `closeRequested`, `exitRequested` or `setQuitOnClose(true)` for that.

`ExitRequestEventArgs::reason` is still the optional text that Windows shows when it blocks a shutdown.

### App callbacks are listeners now, and default listeners run first

The App's own callbacks (`update`, `draw`, `exit`, key, mouse, touch, `windowResized`, `filesDropped` and so on) are now called as a listener on the same event, at priority `EventPriority::App` (`100`). Two things change.

**1. `listen()` without a priority now means `EventPriority::BeforeApp` (`0`), not `100`, on these events.** Other events still default to `100`.

- A default listener ties with an explicit `0` and runs in registration order. A default `a` registered before an explicit-0 `b` now runs `a`, then `b`. Before, it ran `b`, then `a`.
- Default listeners now run before explicit `1`–`99`. Before, they ran after them.
- Default listeners now run before explicit `100`. Before, they shared priority `100` and ran in registration order.
- So a default listener that sets `e.consumed = true` now stops even an explicit-100 listener registered earlier.

```cpp
overlayListener = events().mousePressed.listen([](MouseEventArgs& e) { /* ... */ }, 50);
pickListener    = events().mousePressed.listen([](MouseEventArgs& e) { /* ... */ });
// v0.7: pickListener had priority 100, so it ran after overlayListener.
// v0.8: pickListener has priority 0, so it now runs first.

// To keep it after overlayListener, give it a priority above 50:
pickListener    = events().mousePressed.listen([](MouseEventArgs& e) { /* ... */ }, 60);
```

**2. Listeners above 100 now run after the App.** In v0.7.7 every listener ran first, whatever its priority, and then the App's callback. Now the App runs at 100, together with its node tree:

- A listener at `EventPriority::AfterApp` (`200`), or any priority above `100`, now runs after `App::update()`, `App::draw()`, `App::keyPressed()` and so on. A `draw` listener at 200 now paints over the App.
- A listener that runs before the App and sets `e.consumed = true` now stops the App's callback too. In v0.7.7 the App's callback always ran.
- When the App or its node tree consumes an event, listeners above 100 no longer see it.

```cpp
// v0.7: ran before App::draw(). v0.8: runs after it, so it draws on top.
hudListener = events().draw.listen([] { /* ... */ }, EventPriority::AfterApp);

// To draw underneath the App again, use a priority below 100:
hudListener = events().draw.listen([] { /* ... */ }, 50);
```

Check every listener whose order matters, and give it an explicit priority.

### Node and Mod setup() runs when they join the tree

`Node::setup()` and `Mod::setup()` now follow one rule: each runs once, when the node or mod joins the running App's tree.

- `addChild()` and `insertChild()` run the child's `setup()`, and its subtree's, before they return, if the parent is already set up and in the App's tree. Before, the child's `setup()` ran just before its first update or draw.
- A subtree built while detached is set up as a whole when it is attached: parents before children, children in order, and each node's mods right after the node.
- `addMod()` on a node that is set up and in the tree runs the mod's `setup()` at once, as before. On a node that is not in the tree yet, the mod's `setup()` now waits and runs right after the node's own `setup()`. Before, it always ran inside `addMod()`, even before the node's `setup()`.
- The App itself is still set up on its first update or draw after it becomes a window's App. Children added to it before that are set up right after it.
- Inactive nodes (`setActive(false)`) are set up when they join, not when they first become active.
- A child added inside a node's `setup()` is set up at once, before that node's mods that were added earlier.
- A child added during `update()` is set up inside `addChild()`, and its first `update()` comes on the next frame.
- The update, draw and setup passes now skip a child that was removed or moved to another parent while the pass was running.
- `CoreEvents::setup` (`events().setup`) is removed. It was never fired. Put code that should run after setup at the end of `setup()`.

Set a node's values before `addChild()`, or in its constructor. Values set after `addChild()` returns come too late for `setup()`:

```cpp
// Before
auto ball = make_shared<Ball>();
addChild(ball);
ball->radius = 30;   // Ball::setup() ran later, on the first update

// After
auto ball = make_shared<Ball>();
ball->radius = 30;   // set values first
addChild(ball);      // Ball::setup() runs here
```

Pass what a mod's `setup()` needs as constructor arguments of `addMod<T>(args...)`. A `LayoutMod` lays out its node once when the node is attached. A `RigidBody2D` added to a detached node gets its body when the node joins the tree, at the node's position then. If its `World` has been destroyed by then, no body is created and a warning is logged.

### events() needs an App, and process events are on runtime()

`events()` now returns the event hub of the App that calls it. Called where there is no App, it logs an error and returns a hub that never fires; `listen()` on it registers nothing. This happens in static or global initializers before `runApp()`, and in non-App code (including worker threads) while two or more windows are open. In v0.7.7 `events()` always returned the main window's hub. The same applies to window getters such as `getDeltaTime()` and `getUpdateCount()`, which return `0` there.

App code keeps using `events()`. Non-App code (addons, app-wide handling) uses `runtime().events()` for process events, or the `events()` of a `Window` it holds:

```cpp
// Before: an addon subscribed from anywhere
listener = events().exitRequested.listen([](ExitRequestEventArgs& e) { /* ... */ });

// After: process events are on the runtime
listener = runtime().events().exitRequested.listen([](ExitRequestEventArgs& e) { /* ... */ });
```

`exitRequested`, `deviceLost` and `hotReloadUnload` have the same names on both hubs. The runtime receives them first, then each App, starting from the most recently attached one. (For `hotReloadUnload`, only the Apps being replaced receive it.) `deviceLost` still shares one mutable `cancel` among its listeners.

### Headless apps fire update and exit events

`runHeadlessApp()` now fires `events().update` on every step and `events().exit` when the app ends, with the App as the listener at priority `100`. In v0.7.7 headless apps called the App directly and these listeners never ran. Addon and app listeners that were silent in headless apps now run there.

```cpp
// This listener now runs in a headless app too.
// If it must not, check whether the App has a window:
updateListener = events().update.listen([this] {
    if (!getWindow()) return;   // headless: no window
    /* ... */
});
```

### A hidden window keeps updating but stops drawing

While the OS reports a window as hidden (minimized, fully covered by other windows, or on another Space), `update()` keeps running at its normal rate and only `draw()` stops. The rule is the same for every window on every desktop OS. On iOS and Android, the app's suspension by the OS still pauses updates. In v0.7.7 the main window kept updating and drawing while hidden, and other windows paused both. So:

- A hidden main window no longer draws.
- A hidden secondary window now keeps updating.

Work such as sending OSC from `update()` keeps running while the window is hidden. An option to pause updates while hidden is planned for a later v0.8.x release.

The new `occludedChanged` event tells you when a window becomes hidden or visible.

On Linux (X11), a window now receives `SUSPENDED` when it is minimized or fully covered and `RESUMED` when it is visible again. Before, minimizing did not send `SUSPENDED`. On macOS, a new window no longer receives an initial `SUSPENDED`. This matters only if you listen to `rawEvent`.

### Quitting during setup()

If the App in the launch window asks to quit inside `setup()` and the quit is accepted, the App ends right after `setup()` returns. That is before audio starts and before any console command, `update()` or `draw()`. If the quit is canceled, the app runs normally. A quit requested by an App deeper in the node tree waits until the current pass over the tree is finished, and is handled before the next runtime turn.

```cpp
void tcApp::setup() {
    if (!config.load("settings.json")) {
        exitApp(1);   // ends right after setup(); update() and draw() never run
        return;
    }
}
```

### Logout, restart and shutdown

When the OS logs out, restarts or shuts down, TrussC now ends the app in one of two ways. This works on Windows and macOS; Linux has no receiver for it.

- **No App code is running on the main thread.** `exitRequested` is asked, and listeners can cancel. If the OS goes ahead, the normal ending runs: exit events, `App::exit()`, audio detach and `cleanup()`. All of it shares one time limit, 3 seconds by default; change it with `runtime().setLogoutTimeout(seconds)`. Then TrussC calls `std::exit` with the app's exit code, so `atexit` handlers and static destructors run as in an ordinary exit. The time limit still applies while they run (on Windows, not to the destructors of loaded DLLs, which run after it).
- **App code, or TrussC code serving it, is running on the main thread** (for example inside `update()`, or in a modal dialog). TrussC accepts without asking and runs no App code: no exit events, no `App::exit()`, no audio detach, no `cleanup()`. The OS ends the process. `atexit` handlers and static destructors do not run, and neither do they when the OS kills the process.

So save your app's state in `exit()`, and keep that save short.

```cpp
void tcApp::setup() {
    runtime().setLogoutTimeout(10);   // allow up to 10 s for exit() and cleanup()
}
void tcApp::exit() {
    saveJson(state, "state.json");
}
```

### Hot reload supports one window

- Hot reload works with one window. Each reload replaces the launch window's App between turns of the main loop. Opening a second window while hot reload is active logs a warning each time, and how it behaves is not guaranteed.
- Stop your threads, timers and other async work in `events().hotReloadUnload`. The reload does not wait for that work and does not stop it.
- Every build that loaded successfully stays in memory until the process ends, so many reloads can make the process use more memory.

```cpp
void tcApp::setup() {
    unloadListener = events().hotReloadUnload.listen([this] {
        worker.waitForThread();   // stop the thread before the old code goes away
    });
}
```

### Stdin commands need console::start()

Console input is now opt-in. Neither `runApp()` nor `runHeadlessApp()` reads stdin by itself any more. `CoreEvents::console` (`events().console`) is removed. Listen once on `runtime().events().console`, and call `console::start()` in `setup()`:

```cpp
// Before
consoleListener = events().console.listen([this](ConsoleEventArgs& e) { handleCommand(e); });

// After
void tcApp::setup() {
    consoleListener = runtime().events().console.listen([this](ConsoleEventArgs& e) {
        handleCommand(e);
    });
    console::start();
}
```

- Keep `consoleListener` as an `EventListener` member. The subscription belongs to the App and is removed when the App ends.
- Commands are delivered on the main thread, once per runtime turn: also before the first draw, after the first window closes, with no window open, and in headless apps. You never need to pump them yourself.
- Commands go to the runtime's `console` event only, not to each App. Delivery stops when the App ends.
- `console::stop()` stops delivery and discards the commands that are waiting. `console::start()` also discards waiting commands before it turns delivery on again.
- The first `console::start()` creates the process's only stdin reader. While stopped, the reader keeps reading and discards the lines. It ends at EOF. Starting again does not create a new reader and does not replay discarded lines.
- An app that never calls `console::start()` leaves stdin alone. The console is not available on the web.

### Window and runtime settings

- **The uniform buffer reservation moved to `RuntimeSettings`.** Pass it as the second argument of `runApp()`. The one-argument `runApp<T>(windowSettings)` uses the default runtime settings.

  ```cpp
  // Before
  WindowSettings settings;
  settings.reserveUniformBuffer(8 * 1024 * 1024);
  return runApp<tcApp>(settings);

  // After
  RuntimeSettings runtime;
  runtime.reserveUniformBuffer(8 * 1024 * 1024);
  return runApp<tcApp>(WindowSettings().setSize(1280, 720), runtime);
  ```

- **`WindowSettings::clipboardSize` and `setClipboardSize()` are removed.** Clipboard text and dropped files have no TrussC size limit any more (see [Limits that are gone](#limits-that-are-gone)). If you set up sokol yourself, also remove assignments to `sapp_desc::clipboard_size`, `max_dropped_files` and `max_dropped_file_path_length`, which are removed.

  ```cpp
  // Before
  settings.setClipboardSize(1024 * 1024);
  // After: delete the line
  ```

- **The order of the `WindowSettings` fields changed.** `quitOnClose` now sits between `sampleCount` and `fullscreen`, and `clipboardSize` and `uniformBufferReserve` are gone. If you fill `WindowSettings` by position, the values land in the wrong fields. Use the setters instead.

  ```cpp
  // Before: positional
  WindowSettings settings{1280, 720, "App", true, false, 4, true};
  // After
  auto settings = WindowSettings().setSize(1280, 720).setTitle("App").setFullscreen(true);
  ```

- **The cursor is shared by the whole app.** Its shape, visibility and custom images apply to every window, and it appears in whichever window the pointer is over. An ImGui context changes it only while the pointer is over its own window.
- **`pixelPerfect` is set by the launch window.** In v0.8.0 the launch window's `pixelPerfect` applies to every window.

### Internal Window functions are no longer public

`Window::context()`, `dispatchMousePressToTree`, `dispatchMouseReleaseToTree`, `dispatchMouseMoveToTree`, `dispatchMouseScrollToTree`, `dispatchKeyPressToTree`, `dispatchKeyReleaseToTree`, `tickTree`, `drawTreeNow` and `syncRootSize` are internal now, in C++ and in Lua. There is no public replacement yet: how to send synthetic input is still being designed.

```cpp
// Before
win->dispatchKeyPressToTree(key);
// After: no replacement in v0.8.0; remove the call
```

### No built-in crash handler

TrussC no longer installs a crash handler and no longer writes crash reports. Its API was internal, so no code changes. If you need crash reports, use your platform's debugger or an external crash reporter.

## Async work and threads

### runOnMainThread() runs between turns, and drops work for an ended App

- `runOnMainThread()` now runs its work at the next safe point between turns of the main loop. Before, it ran at the start of the next frame. It now also runs when no window draws.
- Work queued from an App is dropped if that App has ended or is ending when the work would run. Work queued with no App is never dropped.
- For work you queue from your own worker thread that uses an object which may go away, pass a liveness token with the new overload `runOnMainThread(fn, weakToken)`. If the token has expired or is `false` when the work would run, the work is dropped.
- For events from a worker thread, prefer `listen(..., Deliver::Main)`. It copies the event data and runs your listener on the main thread.

```cpp
// Before: copy the data, then queue it
tcp.onReceive.listen([this](Msg& m) {
    auto copy = m;
    runOnMainThread([this, copy] { scene->addChild(make_shared<Enemy>(copy.pos)); });
});

// After: let the event deliver on the main thread
receiveListener = tcp.onReceive.listen([this](Msg& copy) {
    scene->addChild(make_shared<Enemy>(copy.pos));
}, Deliver::Main);
```

### Thread::waitForThread() on its own thread

Calling `waitForThread()` from inside the thread itself now asks the thread to stop (unless you pass `false`) and returns without waiting. In v0.7.7 it threw and the process ended. This is a fix; no change is needed.

## Rendering

### Every angle is in radians; degrees use Deg names

The last functions that took degrees now take radians. The names stay the same, so old code still compiles but passes degrees where radians are expected. Switch to the `Deg` name to keep degrees, or convert the value:

| Before (degrees) | Now (radians) | Keeps degrees |
| --- | --- | --- |
| `setupScreenPerspective(45)` | `setupScreenPerspective(TAU / 8)` (default) | `setupScreenPerspectiveDeg(45)` |
| `setupScreenFov(fov)` | `setupScreenFov(fov)` | `setupScreenFovDeg(fov)` |
| `setDefaultScreenFov(fov)` / `getDefaultScreenFov()` | same names | `setDefaultScreenFovDeg()` / `getDefaultScreenFovDeg()` |
| elliptical `path.arc(c, rx, ry, 0, 270)` | `path.arc(c, rx, ry, 0, TAU * 0.75f)` | `path.arcDeg(c, rx, ry, 0, 270)` |
| Node reflected `rotation` (JSON, MCP, inspector) | `rotation` in radians | `rotationDeg` (derived, not saved) |

- The screen FOV functions accept `0` (orthographic) or `0 < fov < TAU / 2` (`0 < fov < 180` for the `Deg` names). Any other value, such as `60` left over from the degree version, is ignored and the view stays as it was. The first ignored call logs a warning; later ones are silent.
- `0` degrees also means orthographic. `setupScreenOrtho()` and `setDefaultScreenOrtho()` are clearer ways to write `setupScreenFov(0)` and `setDefaultScreenFov(0)`. After either default setter, `getDefaultScreenFov()` returns `0`.
- **Saved JSON:** `rotation` is now read as radians, so a file saved before v0.8.0 (degrees under `rotation`) loads with wrong rotations. Convert those values with `deg2rad()`, or rename the key to `rotationDeg` in the file; that key is still read and converted. New files save only `rotation`, in radians.
- Not an angle, unchanged: hue (`ColorHSB::h`, `ColorOKLCH::H`, 0–1 of a turn).

### Each Fbo has its own matrices, camera and clipping

Each successful `Fbo::begin()` now starts from a clean state:

- the model matrix is identity, and the matrix, camera and scissor stacks are empty;
- the viewport covers the whole Fbo;
- the camera is a TAU/8 perspective in Fbo pixels.

Loading the previous contents (LOAD) keeps the pixels, not the drawing state. `end()` puts back the camera (including its `CameraContext`), model matrix, viewport, clipping and pipeline that were active before `begin()`.

So a transform outside the Fbo no longer affects drawing inside it. Put each transform, and its matching pop, inside the target it draws to:

```cpp
// Before: relied on the outer transform reaching the drawing inside the Fbo
pushMatrix(); translate(20, 10);
fbo.begin(); mesh.draw(); fbo.end();
popMatrix();

// After: each transform stays on its own target
fbo.begin();
pushMatrix(); translate(20, 10); mesh.draw(); popMatrix();
fbo.end();
pushMatrix(); translate(100, 40); fbo.draw(0, 0); popMatrix();
```

- **Balance pushes and pops on the same target.** This covers matrices, scissor, and EasyCam `begin()`/`end()`. Pushes left open inside an Fbo are warned about and discarded at `end()`. A push made on the screen cannot be popped, and does not clip, inside an Fbo.
- **Set clipping inside the Fbo.** Set an offscreen clip after `begin()`.
- **`setMatrix(model)` replaces only the model matrix**, and the GPU gets view × model. Remove any view matrix you multiplied into its argument. Copy screen matrices before `begin()` if you need them inside.
- **EasyCam** uses the aspect ratio of the current target and puts back its entry camera at `end()`. Remove any window-aspect correction you added for Fbo drawing.
- **Screen defaults stay on the screen.** `setDefaultScreenFov` and `pixelPerfect` set up the screen only. For another camera inside an Fbo, call the screen setup functions after each `begin()`. A projection size you set explicitly stays independent of the Fbo size.
- **Sizes inside an Fbo.** `tc::getWidth()`, `tc::getHeight()` and `tc::getSize()` return the Fbo size while it is active, so use them in drawing code. `App` and `RectNode` size getters still describe the object, and window and framebuffer getters still describe the window. `getViewMatrix()` and `getProjectionMatrix()` return the values of the active target.
- **Text and `RectNode` clipping use the target's scale.** A font rasterized on a DPI-2 window keeps its requested size in an Fbo measured in pixels.
- **`setCameraPosition`** applies to the current target and returns to its previous value when the camera or target ends. Its default (zero, set by hand) is unchanged, and so is how lights, materials and environments register with the window.
- **Picking:** the default Fbo camera cannot be picked through; an explicit screen setup and EasyCam can. This is unchanged.
- **Blending:** the logical blend and depth settings carry across targets. Calling `setBlendMode` with the current value now selects that blend pipeline again.

Behavior that changes the pixels:

- **Nested Fbos work.** Calling `begin()` on another Fbo inside an active one now works. v0.7.7 refused it with "Nested fbo.begin() is not supported". Beginning the same Fbo again inside itself, or from another window, is still refused.
- **GPU points follow the model matrix.** Before, points ignored it. Remove any pre-transform you added to make up for that.
- **Point size is in Fbo pixels.** On a DPI-2 window, `setPointSize(12)` inside an Fbo now draws 12 × 12 Fbo pixels; before, it drew 24 × 24. Points on the screen are unchanged. Remove DPI or aspect corrections for points in Fbos.
- **Premultiplied sources blend correctly in Fbos.** Remove any brightening or un-premultiplying you added. Mark only sources that really are premultiplied.
- **The blend mode after `Fbo::end()` applies right away.** Set the blend you want after `end()` if the following screen drawing needs a different one. Remove forced blend reloads that restored the style inside Fbos; push and pop the style normally.
- **Linux NV12 video and LUT drawing use the normal transforms.** Remove CPU positioning, pre-transforms and viewport workarounds that made up for them.

### Scissor rectangles use logical coordinates

This is a fix. `setScissor()` and `pushScissor()` took framebuffer pixels, while every other drawing function takes logical coordinates. So on Retina and other HiDPI displays, a scissor was in the wrong place or had the wrong size. They now take the logical coordinates of the current render target, the same units as `drawRect()`.

If you multiplied by the pixel scale to work around this, remove it:

```cpp
// Before
float s = getDpiScale();
pushScissor(10 * s, 10 * s, 200 * s, 100 * s);

// After: the same rectangle that drawRect(10, 10, 200, 100) draws
pushScissor(10, 10, 200, 100);
```

- Inside an `Fbo` and in pixelPerfect mode, logical coordinates are already pixels, so code there does not change.
- The transform matrix and the camera still do not apply. A scissor is an axis-aligned rectangle of the target, measured from its top-left corner.
- `RectNode::setClipping(true)` needs no change. Nested `pushScissor()` calls still intersect, and `popScissor()` restores the previous rectangle.

### grabScreen() reads the finished frame in afterFrame

`grabScreen()` now depends on when you call it:

- in `update()`, `setup()` or anywhere outside a frame: it returns `false` and logs one warning per window;
- in `draw()`: it reads the frame so far, without ImGui, child nodes drawn later and later overlays. It is slow and meant for debugging;
- in an `events().afterFrame` listener: it reads the finished frame, including ImGui.

v0.7.7 told you to call it outside `draw()`, for example at the end of `update()`. That call now returns `false`. Move it to `afterFrame`:

```cpp
// Before
void tcApp::update() {
    if (captureRequested) { grabScreen(pixels); captureRequested = false; }
}

// After: captureListener is an EventListener member
void tcApp::setup() {
    captureListener = events().afterFrame.listen([this] {
        if (captureRequested) { grabScreen(pixels); captureRequested = false; }
    });
}
```

`grabScreen()` never reads an active Fbo; use `Fbo::readPixels()` for that, and `saveScreenshot()` for files. On the web, `grabScreen()` is not available: it returns `false` and logs a warning once.

### The swapchain pass functions are removed

`ensureSwapchainPass()`, `suspendSwapchainPass()` and `resumeSwapchainPass()` are removed. TrussC now starts and resumes the screen pass by itself. `present()` and `isInSwapchainPass()` remain. To draw offscreen during `draw()`, use `fbo.begin()` and `fbo.end()`, which can now be nested.

```cpp
// Before
suspendSwapchainPass();
fbo.begin(); /* ... */ fbo.end();
resumeSwapchainPass();

// After
fbo.begin(); /* ... */ fbo.end();
```

### Cubemap textures are uploaded in one call

`Texture::uploadCubemapFace()` and `uploadCubemapMip()` are removed. Use `loadCubemapData(faces, faceSize)`, which takes all six faces at once. Dynamic and Stream cubemaps must have exactly one mip level, and `loadCubemapData()` can be called once per frame.

```cpp
// Before
for (int i = 0; i < 6; i++) tex.uploadCubemapFace(i, 0, faceData[i], faceSize);

// After
const void* faces[6] = {faceData[0], faceData[1], faceData[2], faceData[3], faceData[4], faceData[5]};
tex.loadCubemapData(faces, faceSize);
```

### Shader subclasses use pipelineForCurrentTarget()

The protected member `Shader::pipeline` is removed, because each render target now has its own pipeline. A subclass calls the protected `pipelineForCurrentTarget()` instead. This matters only if you subclass `Shader`.

```cpp
// Before
sg_apply_pipeline(pipeline);
// After
sg_apply_pipeline(pipelineForCurrentTarget());
```

### Fbo::readPixels() on float Fbos, and failed allocations

- `Fbo::readPixels()` on a float Fbo now always returns RGBA8 values clamped to 0–255, on every backend. Before, the result depended on the backend; Metal, for example, returned `false`. It now also works on the web with WebGL2 (not with WebGPU). Use `readPixelsFloat()` for full precision.
- If the GPU refuses the size given to `Fbo::allocate()`, `isAllocated()` stays `false` and `begin()`, `draw()` and reading back do nothing. Check `isAllocated()` after allocating large Fbos.

```cpp
// Before: float values from readPixels() on some backends
fbo.readPixels(bytes);
// After: use the float version for float data
vector<float> values(w * h * 4);
fbo.readPixelsFloat(values.data());
```

## Loading and saving

### Saving and recording return SaveResult

The save and record functions now tell you what went wrong, instead of returning a plain `bool` or nothing. `SaveResult` has `ok()`, an `error` (`AccessDenied`, `NoSpace`, `WriteFailed`, `Unknown`) and a `message`. It converts to `bool` explicitly, so `if (img.save("out.png"))` keeps working. Code that stores the result in a `bool`, passes it on as a `bool`, or returns it from a function that returns `bool` needs `.ok()`:

```cpp
// Before
bool saved = img.save("out.png");

// After
bool saved = img.save("out.png").ok();
auto result = saveJson(json, "settings.json");
if (!result) logError() << result.message;
```

`bool` → `SaveResult`:
- `Pixels::save`, `Image::save`, `Fbo::save` and `HasTexture::save` (an override in your own class must return `SaveResult` too)
- `saveJson`, `saveTextFile`, `appendToFile`, `FileWriter::open`, `Xml::save`
- `setLogFile` (and `Logger::setLogFile`, deprecated `tcSetLogFile`)
- `AudioRecorder::start`, `VideoWriter::open` / `addFrame` / `addFrameAt` / `writeAudio` / `submitFrame`, `ScreenRecorder::start`, `startRecording`

`void` → `SaveResult`: `AudioRecorder::stop()`, `VideoWriter::close()`, `ScreenRecorder::stop()`, `stopRecording()`. Ignoring the result still compiles, but finishing a recording can fail (disk full, encoder error), and only this result tells you.

In Lua, test `result:ok()`; the result object itself is always true.

What else changes:
- `Pixels` / `Image` / `Fbo::save`, `saveJson`, `saveTextFile` and `Xml::save` write to `<file>.tmp` and then replace the file, so a failed save leaves the previous file as it was.
- `appendToFile`, `FileWriter`, the log file and the recorders write in place, and check every write.
- `saveScreenshot()` still returns `bool`, meaning "queued". A failed write later logs one error. On the web it is still a browser download.
- `AudioRecorder` ends the recording at the first failed write; `getRecordedSeconds()` counts what was actually written.
- `VideoWriter::getFrameCount()` counts the frames the encoder accepted. A backend that stops accepting data ends the recording. This includes macOS audio input that stays "not ready" for 5 seconds; before, that chunk was dropped while the video kept recording. A frame of the wrong size, or another mistake in the call, fails only that call; the recording goes on.

### OBJ and glTF loading return LoadResult

`tcx::obj::ObjLoader::load()` and `tcx::gltf::GltfModel::load()` now return `LoadResult` instead of `bool`. `if (model.load(path))` keeps working. Code that assigns, passes or returns the result as a `bool` needs `.ok()` or an explicit cast:

```cpp
// Before
bool loaded = model.load("model.gltf");

// After
bool loaded = model.load("model.gltf").ok();
auto result = model.load("model.gltf");
if (!result) { /* result.error and result.message; already logged */ }
```

- A missing file returns `FileNotFound` and logs `file not found: <resolved path>`. Data the loader rejects returns `DecodeFailed` with one error.
- Relative paths are resolved through `getDataPath()`. Neither loader has `getPath()`.
- `GltfModel::load()` now takes `const fs::path&`; a `std::string` still converts. For UTF-8 text on Windows, pass `utf8ToPath(text)` so it becomes a native path.
- External buffer and image URIs in glTF now decode percent escapes, such as `a%20b.png` for `a b.png`.

### OBJ export returns SaveResult

`tcx::obj::ObjExporter::save()` now returns `SaveResult`. `if (exporter.save("model.obj"))` still works. Use `.ok()` when storing or passing the result as a `bool`, or keep the result to inspect the failure:

```cpp
// Before
bool saved = exporter.save("model.obj");

// After
bool saved = exporter.save("model.obj").ok();
auto result = exporter.save("model.obj");
if (!result) {
    // result.error and result.message; save() already logged one error
}
```

- An empty exporter, or a texture that fails to save, now fails the call with one error.
- The OBJ and MTL text files are each written to a temporary file and then replaced. If the MTL write fails, both previous text files stay as they were.
- The replacement is per file, not for the whole export: if closing or replacing the OBJ file fails at the end, the new MTL can sit next to the previous OBJ, and textures already saved are kept.

### LoadError has two new values

`LoadError` gained `TooLarge` and `Cancelled`. A `switch` over `LoadError` without a `default` now warns about the missing cases.

```cpp
switch (result.error) {
    // ... the cases you had
    case LoadError::TooLarge:  /* GPU texture size, URL or network limits, platform size */ break;
    case LoadError::Cancelled: /* a loadAsync() that was canceled */ break;
    default: break;
}
```

### Sound files are found through getDataPath()

`SoundBuffer::load()` and `loadWav()`, `loadMp3()`, `loadOgg()`, `loadFlac()` now resolve a relative path through `getDataPath()`, like images and fonts. In v0.7.7 they opened the path as given, relative to the working directory. `SoundBuffer::getPath()` now returns the resolved path, and is empty after a failed load. You see the same path in `getPlayingSounds()` and the `tc_get_audio_state` MCP tool.

```cpp
// Before: relative to the working directory
buffer.load("data/click.wav");

// After: relative to the data folder (or pass an absolute path)
buffer.load("click.wav");
```

Some sounds whose format changes partway through now fail to load instead of playing wrong: an Ogg/Vorbis file whose channel count changes mid-file, and a frame format change in the FFmpeg and AAC decoders. MP3 and FLAC files are not checked; they still play with the format of their first part.

## Devices

`Serial`, `VideoGrabber`, `DepthCamera`, `MidiIn`/`MidiOut`, `AudioEngine` and `MicInput` now share one small surface, described in [DEVICE_LIFECYCLE.md](DEVICE_LIFECYCLE.md):

- `setup(...)` opens a device; `setup()` with no arguments opens it again;
- `isConnected()` tells you whether it is open;
- `close()` closes it;
- `onDisconnect` (`Event<DeviceDisconnectEventArgs>`, one field, `reason`) fires once when the OS reports the device lost.

`onDisconnect` never fires for your own `close()`, for `setup()` or for the destructor. TrussC does not reconnect by itself: reconnecting is up to your code, for example a retry every 2 seconds:

```cpp
if (!serial.isConnected() && getElapsedTime() - lastTry > 2.0f) {
    lastTry = getElapsedTime();
    serial.setup();
}
```

`DeviceLostEventArgs`, used by the graphics device's `deviceLost` event, is not related and is unchanged.

### Serial

- **The event type changed.** `Event<SerialDisconnectEventArgs>` is now `Event<DeviceDisconnectEventArgs>`, which has one field, `reason`. `SerialDisconnectEventArgs` and its `portName`, `baudRate` and `wasClean` are removed. To reconnect from a listener, call `serial.setup()`, which reopens the last port at the last baud rate. If the listener needs to show the port name or baud rate, keep your own copy; `getDevicePath()` still returns the path.

  ```cpp
  // Before
  serial.onDisconnect.listen([this](SerialDisconnectEventArgs& e) {
      if (!e.wasClean) serial.setup(e.portName, e.baudRate);
  });

  // After
  disconnectListener = serial.onDisconnect.listen([this](DeviceDisconnectEventArgs& e) {
      logWarning() << "serial lost: " << e.reason;
      // reconnect later with serial.setup()
  });
  ```

- **`onDisconnect` fires only when the OS reports the device lost.** Before, `close()` and `setup()` on an open port (which closes the previous port first) also fired it, with `wasClean = true`. A listener that used `wasClean` to tell a lost device from your own `close()` can drop that check.
- `onDisconnect` fires on the thread whose read or write found the loss.
- A device that went away before any read or write noticed is now closed silently by `close()` or `setup()`. Before, that fired `"closed by close()"`.
- **New:** `serial.setup()` with no arguments reopens the last port at the last baud rate. If it fails, it logs an error and returns `false`.
- `isConnected()` and `close()` are unchanged. `isInitialized()` stays as another name for `isConnected()`.

### VideoGrabber

- **`setup(int width = 640, int height = 480)` lost its default arguments.** `setup()` with no arguments is now the reopen call. For a grabber that was never set up it does what it did before (device 0 at 640 × 480, or the device from `setDeviceID()`), so `grabber.setup();` keeps working. Width only no longer compiles:

  ```cpp
  // Before
  grabber.setup(1280);
  // After
  grabber.setup(1280, 480);
  ```

- **An unplugged camera is now reported.** When the OS says the camera is gone, `update()` closes the grabber and fires `onDisconnect` once, on the main thread. `isConnected()` is already `false` then, and the texture and pixels are released. Before, an unplugged camera left `isInitialized()` true and the last frame on screen. Code that assumed `isConnected()` / `isInitialized()` stay true until `close()` should check them. Keep calling `update()`: it is what reports the loss. Moving a grabber does not fire `onDisconnect` either.
- The OS signals used: `AVCaptureDeviceWasDisconnectedNotification` (macOS, iOS), a Media Foundation reader error or an invalidated device (Windows), `ENODEV` from V4L2 (Linux) and the end of the camera track (web). A camera that only stops sending frames is not reported.
- **New:** `grabber.setup()` reopens the last camera at the size of the last `setup(width, height)`. It finds the camera by name in a fresh `listDevices()`, because the order of cameras can change for a moment when one is unplugged and plugged back in. If no camera has that name, it fails; it never opens another camera. Two cameras with the same name can't be told apart, and the first one opens. `setDeviceID()` forgets the name, so the next `setup()` uses that index. Before any camera has opened, it uses the `setDeviceID()` index (device 0 at 640 × 480 if there was none). If it fails, it logs an error and returns `false`. After a reopen, the texture can show the previous image until the first `isFrameNew()`. When you reconnect, also check `isPendingPermission()`:

  ```cpp
  if (!grabber.isConnected() && !grabber.isPendingPermission() && getElapsedTime() - lastTry > 2.0f) {
      lastTry = getElapsedTime();
      grabber.setup();
  }
  ```

- `isInitialized()` stays as another name for `isConnected()`. `isConnected()` is also `false` while a macOS or iOS camera permission request is pending, as `isInitialized()` was.
- Not in v0.8.0: a stable Windows camera ID (`uniqueId` is still the enumeration index), `setDeviceUniqueID()` and choosing a Windows native mode.

### MidiIn and MidiOut (tcxMidi)

Existing code compiles and behaves as before until the OS reports an opened port removed.

- **An unplugged port now closes.** Before, an unplugged port stayed "open" without a message, and `isOpen()` stayed `true`. Now, on CoreMIDI, the ALSA sequencer and WinMM, the port is closed, one warning is logged, and `isConnected()` turns `false` shortly after the removal (on the next runtime turn, plus up to 100 ms on WinMM). An app that never looked at `isOpen()` sees only the warning. WebMIDI does not report removals, and neither does a virtual port opened with `openVirtualPort()`.

  ```cpp
  // Before: isOpen() stayed true after an unplug
  if (midiOut.isOpen()) midiOut.sendNoteOn(1, 60, 100);
  // After: it turns false; reconnect with setup()
  if (!midiOut.isConnected() && getElapsedTime() - lastTry > 2.0f) {
      lastTry = getElapsedTime();
      midiOut.setup();
  }
  ```

- **New:** `onDisconnect` fires once, on the main thread, when the OS reports the opened port removed. The port is already closed and `isConnected()` is `false`. `close()`, opening another port and the destructor never fire it.
- **New:** `midiIn.setup()` / `midiOut.setup()` with no arguments reopen the last port that opened successfully: the same virtual port, or the port with the same name (found again by name, because indexes shift). If it fails, it logs an error and returns `false`. You still open the first port with `openPort()` or `openVirtualPort()`.
- **New:** `isConnected()` and `close()`. `isOpen()` and `closePort()` stay as other names for them.
- Each open physical port keeps a small libremidi observer to hear about removals (a backend thread on ALSA and WinMM). `close()` and the destructor release it.
- On Linux (ALSA), a large `sendSysex()` is now split into 256-byte events and paced on the calling thread, about 4 seconds per MB.

### AudioEngine and MicInput

Everything that compiled before still compiles. The changes are new functions plus one behavior change.

- **When the OS stops the output device, `isConnected()` / `isInitialized()` turn `false`**, and `play()` is refused (counted as "no output device running") until you open the device again. Before, the engine kept reporting `true` and played into a dead device. After a stop, the engine does not reopen or retry by itself.

  ```cpp
  // Before: kept playing into a dead device
  sound.play();
  // After: open the device again, then play
  auto& engine = AudioEngine::getInstance();
  if (!engine.isConnected()) engine.setup();
  sound.play();
  ```

- **New: `engine.onDisconnect`.** It fires once, on the main thread on the runtime's next turn, when the OS or the audio library reports that a running output device opened by name (`AudioSettings::deviceName`) stopped by itself, for example a USB audio interface unplugged or a Bluetooth headset switched off. The device is closed first, and `isConnected()` is already `false`; to keep playing on the system default, call `setup()` inside the listener. A device opened as the system default follows the OS default and never reports a loss. `close()`, `shutdown()` and setting the engine up again never fire it. Whether a stop is reported depends on the OS; where the backend says nothing, the audio stops and `isConnected()` stays `true`.
- **New: `engine.setup()` and `engine.setup(settings)`** are other names for `init()` and `init(settings)`. They open the system default device, and keep the last sample rate, channel count, buffer size and polyphony. They do not keep `AudioSettings::deviceName` or `backend`. To use a specific device, pass `AudioSettings` with `deviceName` each time. This holds for every reopen: after `close()`, after the OS stopped the device, and from `Sound::load*()`, which calls `init()` while the engine is not connected. See [audioDeviceExample](../examples/sound/audioDeviceExample/src/tcApp.cpp) for choosing a device by name with `AudioEngine::listDevices()`.
- **New: `engine.isConnected()` and `engine.close()`.** `isInitialized()` and `shutdown()` stay as other names for them.
- **New: `MicInput` has `setup()`, `isConnected()`, `close()` and `onDisconnect`.** `setup()` is `start()` at the sample rate of the last start. The microphone is always the system default capture device. `isConnected()` is `isRunning()`, and `close()` is `stop()`. `isRunning()` also turns `false` when the OS stops the capture device, which on Windows includes a change of the default microphone; call `setup()` in the `onDisconnect` listener to reopen the default microphone. The web reports no loss.

### DepthCamera backends return an owned Device

This is for authors of `tcx::depthcamera::DepthCamera` backends (addon `tcxDepthCamera`). Replace the backend's `bool openDevice()`, `closeDevice()` and `captureInto()` overrides with `std::shared_ptr<Device> openDevice()`. Put the source's resources and the capture code in a `DepthCamera::Device` subclass:

```cpp
// Before
bool MyCamera::openDevice() override;    // opened the SDK session on the backend
void MyCamera::closeDevice() override;
StreamFreshness MyCamera::captureInto(DepthFrame& dst) override;

// After
struct SensorDevice : DepthCamera::Device {
    // Own the SDK session and everything capture touches here.
    StreamFreshness captureInto(DepthFrame& dst) override;
    ~SensorDevice() override;   // release the SDK session here
};

shared_ptr<DepthCamera::Device> MyCamera::openDevice() {
    return make_shared<SensorDevice>();   // return nullptr if opening fails
}
```

- Copy the configuration from the backend into the Device when it opens. A Device must not keep the backend's address or call its members.
- `captureInto()` still fills a complete frame and returns `StreamFreshness`; only its owner changes.
- The capture run holds the Device until capture ends. `close()` from another thread waits for it. `close()` from inside capture asks it to stop and returns, and the worker releases the Device when it exits.
- Backend destructors no longer need to call `close()` before their members are destroyed. `setup()` closes the previous run before opening a new Device.
- Playback extensions move their `readExtraBlock()` / `decodesBlockType()` overrides into a `PlaybackDepthCamera::PlaybackDevice` subclass, and override the protected `makePlaybackDevice()` factory to return it. Keep custom decoding data in that Device or in shared storage it owns. `setLoop()` updates the active Device's flag, so it can still change looping during threaded playback.
- The external backends (`tcxOrbbec`, `tcxAzureKinect`, `tcxRealSense`) need the same change in their own repositories; the old capture hooks on the backend are removed.

### DepthCamera connection state

Nothing is removed or renamed for apps, and a backend that already uses `std::shared_ptr<Device> openDevice()` compiles unchanged.

- **Changed:** `setup()` now logs an error when `openDevice()` returns no device, in addition to what the backend logged. After a reported loss, a non-threaded `update()` no longer calls `captureInto()` on the lost device.
- **New:** `camera.isConnected()` is true from a successful `setup()` until `close()` or a reported loss.
- **New:** `camera.onDisconnect` fires once per session, only when a backend reports the device lost, with `isConnected()` already `false`. It fires on the thread that reported the loss; register with `Deliver::Main` to run it on the main thread.
- **New, for backend authors:** the protected `reportLost(std::string reason)`. Call it from any thread where the backend already detects a loss. A backend that never calls it never fires `onDisconnect`. Add it in each backend repository, only where the SDK reports the loss.
- Reconnecting is app code, as in `addons/tcxDepthCamera/example-synthetic`:

  ```cpp
  if (!camera->isConnected() && elapsed > 2.0f) camera->setup();
  ```

## Networking and addons

### OscReceiver::setup() owns its port

`OscReceiver::setup(port)` now binds the port for itself alone. Before, it always turned on address and port reuse. So a second receiver on the same port, for example a second copy of the app, also bound without an error, and unicast messages reached only one of them. Now the second `setup()` returns `false` and reports the error through `onParseError` and the log. After a failed `setup()`, `getPort()` returns `0`.

Core `UdpSocket` on Windows does the same: `bind()` on a socket that did not call `setReuseAddress(true)` or `setReusePort(true)` now sets `SO_EXCLUSIVEADDRUSE`, so it owns its port as it already did on macOS and Linux.

If several receivers on one PC share a multicast port, replace `setup()` followed by `joinMulticast()` with one call:

```cpp
// Before
osc.setup(9000);
osc.joinMulticast("239.0.0.1");

// After: reuse, bind and join in one call
osc.setupMulticast(9000, "239.0.0.1");
```

`joinMulticast()` / `leaveMulticast()` still work after `setup()`, but that port is not shared. Unicast apps need no change; check the return value of `setup()` to notice a port that is already in use.

### OSC and MIDI receive queues

Messages that wait to be polled are now limited by size as well as by count: on top of the 1,024-message limit, the queue holds at most 64 MiB. When the queue is full, the oldest whole messages are dropped to make room, and a single message larger than the budget is rejected. `getDroppedMessageCount()` counts both, and the first loss logs a warning once. Messages handled by a listener are not affected. Listeners still run on the receive thread unless you register them with `Deliver::Main`.

```cpp
// If you poll and fall behind, handle messages as they arrive instead:
oscListener = osc.onMessageReceived.listen([this](OscMessage& m) { handle(m); }, Deliver::Main);
```

### TcpServer limits clients to 64 by default

`TcpServer::start(port, maxClients)` now defaults to 64 clients; before, the default `0` meant no limit. A client beyond the limit is closed when it connects, with one warning. Passing `0` now fails with "maxClients must be positive".

```cpp
// Before: 0 = no limit
server.start(9000, 0);
// After: give a number
server.start(9000, 1000);
```

### tcxCurl follows redirects and limits response size

- Redirects are now followed by default (before, they were not). An app that checked for a 3xx status now gets the final response instead. `setFollowRedirects(false)` brings back the old behavior. At most 5 hops, http and https only, and never from https to http. Credential headers such as `Authorization`, `Cookie` and `X-Api-Key` are dropped when a redirect goes to another origin.
- A response larger than 64 MiB now fails, with status code `0` and an empty body. Raise the limit with `setResponseLimit()`.
- Both changes apply to macOS, Windows, Linux and Android. On the web, `HttpClient` is not implemented yet, so neither applies there.

```cpp
// To get the v0.7.7 behavior back:
http.setFollowRedirects(false);
http.setResponseLimit(256 * 1024 * 1024);   // for downloads larger than 64 MiB
```

### tcxWebSocket limits message size

An incoming message larger than 64 MiB now closes the connection (close code 1009; 4009 on the web). A handshake larger than 64 KiB is rejected. Change the limits with `setMaxMessageSize()` and `setMaxHandshakeSize()`.

```cpp
ws.setMaxMessageSize(256 * 1024 * 1024);   // if your server sends larger messages
```

### Box2D ownership and contact events

- **`World`, `Body`, `CircleBody`, `RectBody` and `PolyShape` cannot be moved.** Create them in place, and store or move smart pointers when you need to.

  ```cpp
  // Before
  bodies.push_back(move(body));   // moved a CircleBody
  // After
  bodies.push_back(make_unique<CircleBody>());
  ```

- **User data is reserved.** TrussC uses the `b2Body` user data and the fixture user data. Put your own data in your `Body` subclass.
- **Contact events moved.** Begin and End now run after each sub-step that TrussC runs, in Box2D's order, instead of inside `Step`. Stay runs once per `update()`. Events from a raw `b2World::Step()` wait for the next `World::update()` of a new frame. See the [addon event contract](../addons/tcxBox2d/README.md#collision-events).
- **Destroying inside a listener.** Listeners may destroy bodies or the World, so check borrowed pointers from the event again after releasing anything. Move the cleanup for the destroyed side of an Exit before you release it; the surviving side receives Exit with a null counterpart.
- **Destroy TrussC bodies through TrussC.** Bodies owned by a TrussC `Body` or `RigidBody2D` are destroyed with `Body::destroy()`, by removing the Mod, or with `World::clear()`. Do not call `b2World::DestroyBody` or `DestroyFixture` on them. Raw destroys of bodies you created yourself are tracked while the body still has fixtures (through the destruction listener). If you remove a body's last fixture and then the body, with no `update()` in between, that destroy is not tracked.
- Register your own destruction listener through `World::setDestructionListener()`, so TrussC's internal one keeps working.
- `clear()` keeps the physics world and its settings, and you can use it again right away. A second `setup()` replaces the physics world.

## Limits that are gone

These limits are removed. If your app needs a limit, set your own.

- **Clipboard and file drops:** clipboard text is no longer cut at 65,536 bytes. A drop is no longer limited to 16 files, or to paths of 2,048 bytes. Memory grows with the content; only available memory and the platform limit it. (`WindowSettings::clipboardSize` is removed; see [Window and runtime settings](#window-and-runtime-settings).)
- **Sound polyphony:** `AudioSettings::maxPolyphony` now defaults to `0`, which means no limit (it was `32`), and `AudioEngine::DEFAULT_MAX_PLAYING_SOUNDS` is `0`. The 33rd sound playing at once used to make `Sound::play()` return `false` and was counted in `droppedPolyphonyLimit`; it now plays.
  - `AudioEngine::getMaxPolyphony()`, `AudioDeviceChangedArgs::maxPolyphony` and the `tc_get_audio_state` field report the limit, so they read `0` when none is set, not the number of slots.
  - A positive value still works as a limit. A negative value means no limit, like `0`.
  - `PlayingSoundInfo::slot` no longer stays within a fixed range.
  - `SoundStream::loadStream(path, maxPolyphony)` still defaults to `1`.

  ```cpp
  // To keep the old limit:
  AudioSettings settings;
  settings.maxPolyphony = 32;
  AudioEngine::getInstance().setup(settings);
  ```

- **OSC bundle nesting:** `OscBundle::MAX_NESTING_DEPTH` is removed. Bundles nest as deep as the 65,536-byte packet allows; the packet size limit stays. If you used the constant, delete the check or put your own limit in an `onBundleReceived` handler.
  - `OscBundle` copies are now shallow: a bundle and its copies share their nested bundles, which are never changed once added. A copy costs one level, and the destructor walks the nesting without recursion.
  - `OscBundle::Element` is now `std::variant<OscMessage, std::shared_ptr<OscBundle>>`.
- **Curves:** `kAppendCurveMaxPoints` is removed, and `appendCurve()` no longer refuses more than 100,000 points. Delete code that uses the constant.

  ```cpp
  // Before
  int n = min(count, kAppendCurveMaxPoints);
  // After
  int n = count;
  ```

## Tools, build and MCP

### TrussC.h no longer brings in the HTTP library's system headers

`TrussC.h` used to include the MCP server's HTTP library, and with it several system headers. It no longer does, so every file that includes `TrussC.h` compiles faster. If your code uses one of these without including it, add the include:

- `<regex>` (`std::regex`, `std::regex_match`, ...)
- `<sys/stat.h>` (`stat`, `chmod`, `mkdir`) on macOS and Linux

```cpp
#include <TrussC.h>
#include <regex>   // add if you use regex
```

Socket headers still come in through TrussC's network headers. The common standard headers (`<mutex>`, `<thread>`, `<map>`, `<fstream>`, `<sstream>`, `<atomic>`, `<functional>`, `<chrono>`, ...) also still come in through `TrussC.h`.

### MCP tools need a dev build or a grant

In v0.7.7, `TRUSSC_MCP=1` started the full set of inspection tools. In an ordinary build it now starts only a health check (`ToolLevel::Public`), until your app grants a tool level in `setup()`. `TRUSSC_MCP_HOST` and `TRUSSC_MCP_TOKEN` are ignored outside dev builds, and `TRUSSC_MCP=0` turns the server off. Details are in [AI_AUTOMATION.md](AI_AUTOMATION.md).

```cpp
// Before
// TRUSSC_MCP=1 ./myApp

// After, while developing: a dev build grants Control
// trusscli run --mcp

// After, in a shipped app that should allow inspection:
void tcApp::setup() {
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);   // Control also allows input and scene changes
}
```

`trusscli build --mcp` and `run --mcp` set the CMake option `TRUSSC_MCP_DEV=ON`; a build without `--mcp` sets it `OFF`.

`mcp::registerInspectionTools()`, `mcp::registerControlTools()` and `mcp::registerDebuggerTools()` are deprecated and will be removed in v1.0.0; use `mcp::setToolLevel()` instead. Until then they keep the v0.7.7 rule: they set the tool level but do not open the MCP server, which still needs `TRUSSC_MCP=1` or `mcp::startHttpServer()`. `mcp::setToolLevel()` opens the server by itself.

### MCP window ids

- `tc_list_windows` returns each window's positive `id` instead of `index`, and the `main` field is gone. Pass that `id` as the `window` argument of other tools. Resource list indexes are unchanged.
- `window: 0` for the main window is no longer valid.
- Leaving out `window` while two or more windows are open now returns an error that lists the ids. Before, it meant the main window.

```json
// Before
{"name": "tc_get_screenshot", "arguments": {"window": 0}}
// After: use an id from tc_list_windows
{"name": "tc_get_screenshot", "arguments": {"window": 1}}
```

### The MCP port line is only on the Logger

The extra copy of the MCP port line on stderr is gone. After the port is bound, the server logs `[MCP] HTTP server listening on http://HOST:PORT/mcp` as a Logger notice only: on stdout, in the file set with `TRUSSC_LOG_FILE`, and to `onLog` listeners. Logger lines start with a timestamp and `[NOTICE]`.

If a tool reads the port from stderr, change it to one of these:

- Read the Logger line from stdout or the log file, and look for `HTTP server listening on http://` anywhere in the line. A console level of Warning or higher hides the line on stdout; the log file still gets it as long as its own level allows notices.
- Set a fixed port with `TRUSSC_MCP_PORT=8080` and connect to `http://127.0.0.1:8080/mcp`. If that port is already in use, the server does not start.

```sh
# Before: read the port from stderr
./myApp 2> >(grep -m1 'listening')
# After: use a fixed port
TRUSSC_MCP_PORT=8080 ./myApp
```

### Shader targets in custom CMake

`<target>_shaders` is now an INTERFACE library instead of a custom target. The generator step is `<target>_shaders_gen`, and `trussc_shader_object_depends()` is new. Ordinary apps are not affected. If your own CMake adds dependencies on `<target>_shaders` as a custom target, depend on `<target>_shaders_gen` instead.

```cmake
# Before
add_dependencies(myTool myApp_shaders)
# After
add_dependencies(myTool myApp_shaders_gen)
```

## New in v0.8.0

You do not need these to upgrade.

- **Loading in the background:** `Image::loadAsync()`, `SoundBuffer::loadAsync()` and `Font::loadAsync()`, with `onLoad` on a later frame. Canceling never waits. `setResourceLoadConcurrency()` / `getResourceLoadConcurrency()` set how many loads prepare at once (4 by default). See [ASYNC_LOADING.md](ASYNC_LOADING.md).
- **Screenshots and microphone without blocking:** `saveScreenshotAsync()` with `onScreenshotSave()`, and `MicInput::startAsync()` with `onStart` and `isStarting()`.
- **Windows and quitting:** `closeRequested` and `occludedChanged` events, `WindowSettings::setQuitOnClose()` and `Window::setQuitOnClose()`, `Window::destroyApp()`, `runtime().setLogoutTimeout()`, `Window::setIndependentFps()`.
- **Runtime settings:** `RuntimeSettings` and `runApp<T>(windowSettings, runtimeSettings)`.
- **Cameras:** `setupScreenFovWithSize()` / `setupScreenFovWithSizeDeg()` for an explicit viewport size; `setDefaultScreenOrtho()`.
- **Devices:** the reopen calls, `isConnected()`, `close()` and `onDisconnect` described in [Devices](#devices).
- **Hot reload:** `TC_NO_HOT_RELOAD("reason");` declares that code cannot be hot-reloaded, so a hot-reload build stops with that reason. tcxHap declares it on Windows, so a hot-reload build that uses tcxHap on Windows now stops with this message; remove `TC_HOT_RELOAD` to build normally.
- **trusscli:** `-j` / `--jobs`, `build --dry-run`, and `build` / `run --mcp`.
- **tcxImGui:** `imguiBegin()` / `ImGuiManager::begin()` now return `bool`; submit widgets only when it returns `true`. Existing code still compiles. New `setup(initialVertices)`, `setupOffscreen()` and `scoped()`. ImGui can now draw into an Fbo.
