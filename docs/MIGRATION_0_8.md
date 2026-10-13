# Migrating to v0.8.0

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

The screen FOV functions accept only `0 < fov < TAU/2` (180 degrees for the `Deg` names). Any other value, such as 60 left over from the degree version, logs one warning and is ignored, so the view stays as it was. Use `setupScreenOrtho()` for an orthographic view.

Saved JSON: `rotation` is now read as radians, so a file saved before v0.8.0 (degrees under `rotation`) loads with wrong rotations. Convert those values with `deg2rad()`, or rename the key to `rotationDeg` in the file, which is still read and converted. New files save only `rotation`, in radians.

Not angles, unchanged: hue (`ColorHSB::h`, `ColorOKLCH::H`, 0–1 of a turn) and `tcx::geo::LatLon` (geographic degrees).
