# AI Automation & MCP Integration

## What is this?

TrussC applications natively support the **Model Context Protocol (MCP)**.
This allows AI agents (like Claude, Gemini, or IDE assistants) to directly connect to, inspect, and control your running application via standard JSON-RPC messages over HTTP.

By enabling MCP mode, your app becomes a "tool" for AI, enabling:

- **Autonomous Debugging:** AI can read logs, check app state, and fix bugs.
- **Automated Testing:** AI can simulate user input (mouse/keyboard) and verify results via screenshots.
- **Game Agents:** AI can play games against humans by reading the game state directly.

## Enabling MCP Mode

To start your app in MCP mode, set the `TRUSSC_MCP` environment variable to `1`.

```bash
# Auto-assign port (printed on startup, see below)
TRUSSC_MCP=1 ./myApp

# Or specify a port (a guaranteed, known port)
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8080 ./myApp
```

When enabled:
1. An **HTTP server** starts on the specified port (or an OS-assigned port).
2. **Inspection tools** (`tc_get_screenshot`, `tc_save_screenshot`) are automatically registered.
3. Once the port is bound, the server endpoint URL is printed: `[MCP] HTTP server listening on http://127.0.0.1:PORT/mcp`. The line is a Logger Notice, so it also reaches the log file (`TRUSSC_LOG_FILE`) and `onLog` listeners; on the console it goes to stdout, and it is hidden when the console level is Warning or higher. In v0.7 the same line is also written raw to stderr, as before (that copy is removed in v0.8.0). For a port that does not depend on reading this line, set `TRUSSC_MCP_PORT`.

### Related: `TRUSSC_LOG_FILE`

Independent of MCP mode, setting `TRUSSC_LOG_FILE=/path/to/app.log` makes a
native app call `setLogFile()` before the window and graphics start
(`runApp()`, before `sapp_run()`), so every log line — including setup-time
output — is appended to that file with zero app code. (Web builds don't read
`TRUSSC_LOG_FILE`.) A relative value resolves against the data folder
(`getDataPath()` with the root in effect when `runApp()` starts — normally
the default; it is read before `setup()`, so a `setDataPathRoot()` in `setup()`
does not apply, while one in `main()` before `runApp()` does), and a
missing parent folder is created. If the file still cannot be opened, the app
runs on and logs a warning. sokol's own errors, warnings and panics go through the
logger too, and lines logged from worker threads land whole. A window or GPU
setup failure reaches the file where sokol reports it as text: on Linux (no X
display; GLX setup, framebuffer config, GL context or window creation; EGL
setup in GLES3 builds), Windows (D3D11 device, main window and swapchain),
macOS (Metal device and main window), and iOS (Metal swapchain textures). On the web,
WebGPU instance, adapter and device request failures reach the logger (the
browser console and `onLog`), not a file. On Android sokol's app messages
(lifecycle, the app thread's startup) reach the logger too, but an EGL setup
failure is not logged. This is how a supervisor process (e.g. `anchorbolt start`)
captures logs from an unmodified app.

On desktop, `runApp()` returns **1** when window or GPU startup fails before
`setup()` runs, and **0** otherwise; `TC_RUN_APP` passes that status to `main()`.
A supervisor can use the exit code together with `TRUSSC_LOG_FILE` to distinguish
a failed start from a normal shutdown. Fatal panics still abort the process.
On Linux, no available X display causes an abort (a nonzero process status).
Android and Web have OS/browser-owned loops; this return value does not report
their eventual shutdown.

The audio engine reports through the logger too, so the file also receives
the plays it had to drop (`Sound::play()` returned false: every playback slot busy, a
stream's `maxPolyphony`, an unreadable stream file, no output device).
Repeats are summed into at most one line per drop reason every 2 seconds,
nothing is logged from the audio thread itself, and what is still held back
is logged on exit. `tc_get_audio_state` (below) reports the same counts.

## Transport

TrussC uses **HTTP transport** for MCP. All JSON-RPC messages are sent as HTTP POST requests to the `/mcp` endpoint.

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC request → response. The body must be sent as `Content-Type: application/json` |
| GET | `/` | Server info (for port discovery) |

## Standard MCP Tools

Tool names are namespaced by origin, so the framework never collides with
your own tools:

- **`tc_*`** — provided by TrussC core (registered by default, or generated
  by core sugar like `mcp::status()`)
- **`tcx_<addon>_*`** — provided by an official addon (e.g. `tcx_imgui_click`)
- **anything else** — yours; custom tools created with `mcp::tool()` should
  NOT use these prefixes

### Inspection Tools (always available in MCP mode)
| Tool | Arguments | Description |
|------|-----------|-------------|
| `tc_get_screenshot` | `format`, `width`, `quality`, `window` (all optional) | Screenshot as an MCP image content block (rendered inline by MCP clients) plus a text metadata block. Defaults to full-resolution lossless PNG; pass `width` for a downscaled monitoring thumbnail (aspect preserved, never upscales, clamped 16-4096) and `format: "jpg"` (+ `quality`, default 75) for small payloads. `window` = index from `tc_list_windows` (default 0 = main). Cheap to poll at any settings: only the framebuffer readback touches the frame loop — downscale + encode run on the HTTP worker thread (measured under continuous hammering at jpg/512: ~179 fps vs ~46 fps for the old synchronous encode; baseline ~236). A secondary window is captured inside its own frame, so it must be visible: see [Hidden secondary windows](#hidden-secondary-windows) |
| `tc_save_screenshot` | `path`, `window`? | Save screenshot to file. Optional `window` index from `tc_list_windows` (default 0 = main). A secondary window must be visible, as for `tc_get_screenshot` |
| `tc_list_windows` | (none) | List open windows: `{windows: [{index, main, title, width, height, occluded}]}`. Index 0 = main (no `title`, no `occluded`), then the secondary windows. `occluded` is `true` while the OS reports that window hidden (`Window::isOccluded()`). Use the index as the `window` arg above |
| `tc_get_audio_state` | `devices` (optional, default `true`) | Audio engine diagnostics, read-only (never starts the engine): `running`; `output` `{device, default, backend, sampleRate, channels, requestedBufferSize, periodFrames, deviceSampleRate, deviceChannels, maxPolyphony}` (`requestedBufferSize` = `AudioSettings::bufferSize` as asked, 0 = backend default; `periodFrames` = the period the device granted); `input` `{running, device, sampleRate}` (the `getMicInput()` microphone); `playingSounds` `[{slot, path (as given, the same string as `getPath()` and the logs, UTF-8), streaming, position, duration, volume, pan, speed, loop, paused, level}]` (`level` = the playback's output peak in the last callback); `master` `{peak, rms, clippedSamples}` (linear, measured before the clamp); `dropped` `{total, polyphonyLimit, streamLimit, decoderError, notRunning}` (plays refused since startup; `polyphonyLimit` = every playback slot busy); `thread` `{cpuUsage, cpuUsagePeak}` (fraction of audio-thread time: mix time / audio time over ~0.5 s of audio, 1.0 = a callback took as long as the audio it produced; `cpuUsagePeak` = the worst single callback, > 1 = a dropout); `devices` `{playback, capture}` lists. Meters and levels read 0 while the engine is not running. Pass `devices: false` to skip the enumeration when polling (it can be slow on some backends). Same numbers as `AudioEngine::getStats()` / `getPlayingSounds()` |
| `tc_get_audio_spectrum` | `n`?, `window`?, `channels`?, `fmin`?, `fmax`?, `peaks`? | Full output FFT: `n` is a power of two from 64 up to the two-second ring length (default 1024); `window` is `hann` (default) or `blackmanharris`; `channels` is `mix` (default, all-channel average) or `each`; `peaks` defaults to 3. Returns `{sampleRate, n, binHz, framesWritten, channels: [{peak, rms, peaks: [{hz, dbfs}], spectrum}]}`. Peak/RMS are linear; spectrum is amplitude dBFS rounded to one decimal, with `null` for zero amplitude (−∞). All n/2+1 bins are returned unless inclusive `fmin`/`fmax` bounds select a range. Bin frequencies start at `ceil(fmin / binHz) * binHz`. Peaks use coherent-gain normalization and frequency interpolation. |
| `tc_save_audio_capture` | `path`, `seconds`? | Save the last `seconds` (default 1.0), ending at the call, as float32 WAV. Returns `{path, sampleRate, channels, frames, framesWritten}`. Relative paths resolve through `getDataPath()`; missing directories are created; paths without a `.wav` extension get `.wav` appended with a log warning. Duration is capped by the two-second ring and available history. Use `AudioRecorder` for longer recordings. |
| `tc_get_health` | (none) | Lightweight liveness snapshot: `{fps, frameCount, uptimeSec, width, height, version, pid, rssBytes, mainQueuePending, memoryBytes}`. Reads counters only (no GPU state), so it is cheap enough for a supervisor to poll. `pid` lets a supervisor confirm the reply comes from *its* child (port collisions); `rssBytes` is whole-process resident memory (the leak-hunting number); `mainQueuePending` is how many `runOnMainThread` / `Deliver::Main` calls this frame's drain started with (each frame runs only those, so frames keep starting; a number that keeps growing means workers queue faster than the app runs them); `memoryBytes` is sokol-tracked allocations only |
| `tc_get_status` | (none) | App-published ops status (see [Publishing custom ops status](#publishing-custom-ops-status)): `{values: [{name, value, mode}], images: [names]}`. `mode` is `"status"` (show as-is) or `"graph"` (plot over time). Empty when the app publishes nothing |
| `tc_get_status_image` | `name`, `width`, `quality` (last two optional) | Fetch an app-published image registered via `mcp::statusImage()`, downscaled + JPEG-encoded exactly like `tc_get_screenshot` (pixel grab on the main loop, encode on the HTTP worker — no frame stutter) |
| `tc_get_alerts` | - | Drain operator alerts raised via `mcp::alert()` — returns and clears the pending list, so exactly one consumer receives each alert |
| `tc_get_node_tree` | `id`, `depth` (both optional) | Dump the node tree (or a subtree) as JSON: per node `{type, name, id, members, mods, children}`. Members are the `TC_REFLECT`ed values — rotation as euler degrees, colors as `[r,g,b,a]` floats 0-1, Vec3 as `[x,y,z]`, enums as their label string. `mods` lists each attached Mod as `{type, members}`. `depth` limits recursion (~270 bytes/node — on large scenes, explore with `depth` + drill into subtrees by `id`; cut-off nodes carry a `childCount`) |
| `tc_get_selected_node` | (none) | The currently selected node (same shape, no children), or `null` |

Both audio tools read the same per-channel, post-clamp output history and never
start the engine. `framesWritten` identifies the end of the snapshot and counts
frames since the latest engine initialization; reinitialization clears history.
Before any initialization, the tools return an error. After shutdown, retained
history remains readable. Spectrum analysis zero-pads missing startup frames;
capture writes only actual frames and fails if none are available. If concurrent
callbacks prevent a coherent snapshot, the tools return an error to retry. Capture does
not wait for new audio. The existing `getAudioAnalysisBuffer()` reads this same
ring while keeping its 4096-sample limit and L/R mono average.

#### Hidden secondary windows

A secondary window renders only while it is visible, and the screenshot tools
capture it inside its own frame. So:

- While the OS reports the window hidden, `tc_list_windows` shows
  `"occluded": true` for it, and `tc_get_screenshot` / `tc_save_screenshot`
  fail at once with `window N is not visible (the OS reports it hidden: ...),
  so it renders no frames: make it visible and retry`. The flag is the OS
  signal that pauses the window, not a diagnosis, so raising the window does
  not always help: macOS: minimized, fully covered or on another Space
  (NSWindow occlusionState); Windows: minimized, or DXGI reports the window
  occluded (while the session is locked or the display is off; under DWM a
  window that is merely covered keeps rendering and is not flagged); Linux
  (X11): minimized, or fully obscured (only reported without a compositing
  manager; under a compositing WM such as GNOME's a covered window keeps
  rendering and is not flagged).
- Otherwise the request waits for the window's next frame. If none comes
  within 5 s (the window became hidden after the check, the platform has no
  signal for how it is hidden, or `Window::setFps()` throttles it very low),
  it fails with `the window rendered no frame within 5 s (minimized, hidden
  or closed?)`.
- The main window (index 0) keeps running while hidden, so none of this
  applies to it.

### Recording Tools (always available in MCP mode)

The video counterpart of `tc_save_screenshot` — capture the window to a video file with the native encoder (no ffmpeg). Always registered when MCP is enabled, but unlike the inspection tools these *write* (start/stop a recording), so they are listed separately.

| Tool | Arguments | Description |
|------|-----------|-------------|
| `tc_start_recording` | `path`, `duration`, `fps`, `codec` (all optional) | Start recording the window. Omit `path` for a timestamped `recording-<timestamp>.mp4` (`.mov` for ProRes) in the data dir; relative paths resolve under the data dir. `duration` (seconds) makes a fixed-length clip that **auto-stops and finalizes itself** at exactly that length (omit or `0` = unlimited). `fps` is the target frame rate (default 60; ProMotion frames are decimated to it). `codec` is `h264` (default) / `hevc` / `prores422` / `prores4444`. Returns `{status, path (resolved), fps, codec}` plus `duration` when a fixed length was set |
| `tc_stop_recording` | (none) | Stop the current recording and finalize the file. A manual stop **always wins** over a pending fixed `duration` — it finalizes immediately at the current length. Returns `{status, recording:false, path, frames, length}` (`length` = measured output seconds). Idle is not an error: returns `{status:"ok", recording:false, message:"not recording"}` |

### Control Tools (opt-in via `mcp::registerControlTools()`)

Everything that lets the outside *operate* the app — the always-on set above is
strictly read-only. (Renamed from `registerDebuggerTools()` — the old name still
works, deprecated until v1.0.0.)

| Tool | Arguments | Description |
|------|-----------|-------------|
| `tc_quit` | (none) | Quit the application gracefully |
| `tc_mouse_move` | `x`, `y`, `button` (optional) | Move cursor; with `button` held, emits a drag |
| `tc_mouse_press` | `x`, `y`, `button` | Press and hold — start of a drag gesture |
| `tc_mouse_release` | `x`, `y`, `button` | Release — end of a drag gesture |
| `tc_mouse_click` | `x`, `y`, `button`, `shift`/`ctrl`/`alt`/`super` (optional) | Click mouse button (0:left, 1:right), optionally with modifier keys held — e.g. `super: true` Cmd+clicks |
| `tc_mouse_scroll` | `dx`, `dy` | Scroll mouse wheel |
| `tc_key_press` | `key`, `shift`/`ctrl`/`alt`/`super` (optional) | Press a key (sokol_app keycode; letters are uppercase ASCII, `'A'`=65). A modifier flag presses that modifier's own key first, so both `e.shift` and `isShiftPressed()` see it |
| `tc_key_release` | `key`, `shift`/`ctrl`/`alt`/`super` (optional) | Release a key; a modifier flag releases that modifier's own key afterwards |
| `tc_select_node` | `id` | Select a node by instance id (0 clears); drives the same selection an inspector shows |
| `tc_set_node_members` | `id`, `members`, `mod` (optional) | Set reflected members from a JSON object (same encoding as `tc_get_node_tree`; enums accept label string or int). Pass `mod` (a Mod's short type name, e.g. `"LayoutMod"`) to target a mod attached to the node instead of the node itself. Reports `applied` / `skipped` (type mismatch) / `readOnly` / `unknown` keys |

Injected key events go through the same two channels a real key does: the
`keyPressed` / `keyReleased` callbacks **and** the held-key set that
`isKeyPressed()` / `isShiftPressed()` read — so an app that polls in `update()`
reacts to injection just like it does to the keyboard. `tc_key_press` /
`tc_key_release` reply with the resulting `heldKeys`, which is the quickest way
to see the state an app is actually looking at.

The modifier flags are shorthand for pressing that modifier's **own key**
(`shift` → keycode 340, `ctrl` → 341, `alt` → 342, `super` → 343), the way a
real keyboard does it — pair the same flag on press and release. To hold a
modifier across several keys, press and release its keycode explicitly; the
presses in between inherit it:

```
tc_key_press   {"key": 340}          # Shift down
tc_key_press   {"key": 65}           # 'A' arrives with shift = true
tc_key_release {"key": 65}
tc_key_release {"key": 340}          # Shift up
```

The node tools make a scene **round-trippable for agents**: arrange things in a
GUI (e.g. with the `tcxNodeInspector` addon's gizmo), then `tc_get_node_tree` to
read the exact values back and bake them into code — or drive the scene the
other way with `tc_set_node_members`.

To enable the control tools, call `mcp::registerControlTools()` in your `setup()`.
Registering the tools *is* the opt-in — there is no separate enable step, and
`mcp::isDebuggerEnabled()` will report `true` once they are registered:

```cpp
void tcApp::setup() {
    mcp::registerControlTools();
}
```

These tools are inert unless the MCP server is also running (`TRUSSC_MCP=1`).

### ImGui Tools (requires tcxImGui addon)

The **tcxImGui** addon provides additional MCP tools for AI agents to inspect and interact with ImGui widgets. To use these, add `tcxImGui` to your project via `trusscli addon add tcxImGui` or `addons.make`, then call `imguiSetup()` before `mcp::registerControlTools()`.

| Tool | Arguments | Description |
|------|-----------|-------------|
| `tcx_imgui_get_widgets` | `window`, `windowId` (optional) | Widgets drawn in the last frame, in every window running imgui: label, `window` (ImGui panel), `windowId` (OS window, `tc_list_windows` numbering), type, rect, and for value widgets `widget` / `valueType` / `value` — floats at full precision, N-component widgets (`DragFloat3`) as one array under their own label, colors as the variable holds them (0-1, `colorSpace` `rgb`/`hsv`), `SliderAngle` in radians, a `RadioButton(int*)` with `buttonValue` (the value that button sets). `touched` = changed by hand |
| `tcx_imgui_get_touched` | (none) | What the user changed by hand since startup or the last reset, with current values. `widgets`: the ImGui value widgets changed — sliders, drags, inputs, colors, combos, text, `Checkbox`, `MenuItem` / `Selectable` with a `bool*`, `RadioButton` with an `int*`, `ListBox` (under its own label, with the index) — each with the value of its variable (not drawn right now = last known value, `visible: false`). Items that change no variable are not recorded: buttons, menu headers, action menu items, `MenuItem(label, shortcut, bool selected)` even as a toggle, plain `Selectable`s, `RadioButton(label, bool)`; `inspector`: tcxNodeInspector edits per node (node type / name / id, mod, member path, value in the `tc_get_node_tree` encoding). Code assignments, `tc_set_node_members` writes and values set by `tcx_imgui_input` on a value widget are not recorded |
| `tcx_imgui_reset_touched` | (none) | Clear that record (both lists). No value changes |
| `tcx_imgui_click` / `tcx_imgui_input` / `tcx_imgui_checkbox` | `label` + tool args, `window` / `windowId` (optional) | Drive a widget by label. `tcx_imgui_input` is for widgets that hold a value (and text fields); buttons and other items without a variable are pressed with `tcx_imgui_click`. `tcx_imgui_input` on a value widget takes the value as JSON in the units `tcx_imgui_get_widgets` reports (`[x, y, z]`, colors 0-1, radians, `true`, an index; for a `RadioButton(int*)` target the button whose value you want: another button's value is refused), writes it into the variable on the next frame (the widget returns true in that frame, so `if (DragFloat(...)) apply();` code runs, unless the variable already held the value) and reads it back, then checks it again in the frame after; text fields are typed into, items with no variable (buttons, `RadioButton(label, bool)`) are refused with a pointer to `tcx_imgui_click`. A composite widget (`DragFloat3`, `ColorEdit4`) cannot be clicked |

The "tweak by hand, then bake" loop: the user adjusts sliders / the inspector in
the running app → `tcx_imgui_get_touched` → write the values into the source →
`tcx_imgui_reset_touched`.

See [addons/tcxImGui/README.md](../addons/tcxImGui/README.md) for full details on available tools and setup.

## Publishing Custom Ops Status

Apps can publish their own monitoring data — one line per value, no
supervisor-side configuration. A supervisor (e.g. `anchorbolt start`)
discovers the `tc_get_status` tool via `tools/list` and forwards the
payload to its monitoring server, where numbers registered with
`statusGraph` are plotted over time and images become live streams in the
dashboard.

```cpp
void tcApp::setup() {
    mcp::status("scene",         [&] { return sceneName; });     // shown as-is
    mcp::statusGraph("visitors", [&] { return visitorCount; });  // plotted over time
    mcp::statusImage("entranceCam", [&] { return camPixels; });  // e.g. a webcam
}
```

- `mcp::status(name, getter)` — a string or number, displayed as-is
- `mcp::statusGraph(name, getter)` — a number that wants to be a time series
- `mcp::statusImage(name, getter)` — `Pixels` fetched on demand via
  `tc_get_status_image` (a webcam feed turns your installation's spare camera
  into a monitoring camera with this one line)

Getters run on the main loop inside tool handlers, so they can safely read
app state. Registering the same name again replaces the entry.

### Operator Alerts

For events a human should hear about — a sensor disconnected, a help button
pressed — the app can raise an alert:

```cpp
mcp::alert("IR camera disconnected!");
```

The message is written to the log (WARNING level) and queued; a supervisor
drains the standard `tc_get_alerts` tool on its health cadence and forwards
each entry to its notification sinks (Slack / Discord / ntfy...), so an
alert can literally end up on someone's phone. It is deliberately named
**alert**, not notify: raise them for exceptional, human-relevant events,
not as a message bus. Thread-safe (callable from sensor callbacks and async
timers); the queue is bounded at 100 pending, oldest dropped first.

## Creating Custom Tools

You can easily expose your own application logic to AI using the `mcp::tool` builder in `setup()`.

```cpp
#include <TrussC.h>

void tcApp::setup() {
    // Expose a function as an MCP tool
    mcp::tool("place_stone", "Place a stone on the board")
        .arg<int>("x", "X coordinate (0-7)")
        .arg<int>("y", "Y coordinate (0-7)")
        .bind([this](int x, int y) {
            bool success = board.place(x, y);
            return json{
                {"status", success ? "ok" : "error"},
                {"turn", (int)board.currentTurn}
            };
        });

    // Expose state as an MCP resource
    mcp::resource("app://board", "Current Board State")
        .mime("application/json")
        .bind([this]() {
            return board.toJSON();
        });
}
```

## Protocol Details

TrussC implements a subset of the **MCP (Model Context Protocol)** specification over HTTP.

### Request (AI -> App)
```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
        "name": "place_stone",
        "arguments": { "x": 3, "y": 4 }
    }
  }'
```

### Response (App -> AI)
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [{ "type": "text", "text": "{\"status\":\"ok\"}" }]
  }
}
```

Image tools (`tc_get_screenshot`, `tc_get_status_image`) return an
MCP-standard **image content block** — clients like Claude Code render it
inline instead of receiving a Base64 wall — followed by a text block with
the metadata:

```json
{
  "result": {
    "content": [
      { "type": "image", "data": "<base64>", "mimeType": "image/png" },
      { "type": "text", "text": "{\"width\":1920,\"height\":1200}" }
    ]
  }
}
```

## Testing with curl

```bash
# Start app in MCP mode
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8080 ./bin/MyApp.app/Contents/MacOS/MyApp &

# Initialize
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{}}'

# Take screenshot
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"tc_save_screenshot","arguments":{"path":"/tmp/test.png"}}}'

# Mouse click (requires registerControlTools())
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"tc_mouse_click","arguments":{"x":100,"y":200}}}'

# Record a fixed 3-second clip (auto-stops & finalizes itself); omit "duration"
# for an unlimited recording you end with tc_stop_recording. Omit "path" for a
# timestamped file in the data dir; the response carries the resolved path.
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":4,"params":{"name":"tc_start_recording","arguments":{"path":"/tmp/clip.mp4","duration":3}}}'

# Stop early (a manual stop always wins — valid shorter file); no-op if idle
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":5,"params":{"name":"tc_stop_recording","arguments":{}}}'
```

### Taking Screenshots from Shell

**IMPORTANT for AI agents**: Do NOT use macOS `screencapture`, OS screen recorders (QuickTime, `ffmpeg`-avfoundation, OBS, etc.), or similar OS commands. TrussC apps may render with Metal/OpenGL and the OS cannot capture the screen correctly. Always use the MCP tools — `tc_save_screenshot` for a still, `tc_start_recording` / `tc_stop_recording` for video.

```bash
# Start app, wait, take screenshot, then kill
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8080 ./bin/myApp.app/Contents/MacOS/myApp &
sleep 2
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{}}'
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"tc_save_screenshot","arguments":{"path":"/tmp/screenshot.png"}}}'
kill %1
# Screenshot is now at /tmp/screenshot.png
```

## Connecting with AI Agents

### Via MCP Clients (Claude Desktop, etc.)

Configure your MCP client with the HTTP URL:

```json
{
  "mcpServers": {
    "trussc-app": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

### Port Discovery

- If `TRUSSC_MCP_PORT` is set, the app uses that port.
- If not set (or set to `0`), the OS assigns an available port.
- The actual port is printed on startup, once the port is bound: `[MCP] HTTP server listening on http://127.0.0.1:PORT/mcp`. It is a Logger Notice (stdout, the log file, `onLog`; hidden on the console when the console level is Warning or higher), plus, in v0.7 only, a raw copy on stderr.
- For a guaranteed port, set `TRUSSC_MCP_PORT` rather than reading the line.
- From code: `mcp::getHttpPort()` returns the actual port number.

## Security Model

| Category | Tools | Enabled by |
|----------|-------|------------|
| Inspection (read-only) | `tc_get_screenshot`, `tc_save_screenshot`, `tc_get_health`, `tc_get_audio_state`, `tc_get_audio_spectrum`, `tc_save_audio_capture`, `tc_get_node_tree`, `tc_get_selected_node` | Automatic when MCP is enabled |
| Recording (window capture to video) | `tc_start_recording`, `tc_stop_recording` | Automatic when MCP is enabled |
| Control (input injection / scene mutation / quit) | `tc_mouse_click`, `tc_mouse_press`, `tc_mouse_release`, `tc_key_press`, `tc_mouse_move`, `tc_mouse_scroll`, `tc_key_release`, `tc_select_node`, `tc_set_node_members`, `tc_quit` | `mcp::registerControlTools()` |
| ImGui (widget reading / interaction) | `tcx_imgui_get_widgets`, `tcx_imgui_get_touched`, `tcx_imgui_reset_touched` (read-only: the reset clears the record, not app state), `tcx_imgui_click`, `tcx_imgui_input`, `tcx_imgui_checkbox` | Requires tcxImGui addon + `mcp::registerControlTools()` |
| Custom | `mcp::tool(...)` | Your code |

### Network exposure

By default the MCP server binds to **127.0.0.1 only** (loopback) and sends no
CORS headers, so it is reachable only by native MCP clients on the same machine
(a wildcard CORS origin would otherwise let any web page in your browser drive
it). The default is the address `127.0.0.1` rather than the name `localhost`,
because what `localhost` resolves to differs between OSes; one address keeps it
the same everywhere. The server is for native MCP clients: a web page cannot
call it, neither directly nor through a dev-server proxy that forwards the
page's `Origin`. For remote access, SSH tunnelling is the simplest safe option.

A web page can still *send* requests to a loopback server without CORS, so
every request is also checked before anything runs (as the MCP HTTP transport
spec requires):

| Check | Refused with |
|-------|--------------|
| When bound to loopback, `Host` must be `localhost`, `127.0.0.1` or `[::1]` (any port) — a DNS-rebinding page arrives under its own name | 403 |
| An `Origin` header, if present, must be the server's own (`http://localhost:PORT`, `http://127.0.0.1:PORT`, `http://[::1]:PORT`) — a browser page on any other origin, including another localhost port, is refused. Native MCP clients send none | 403 |
| `POST /mcp` must be `Content-Type: application/json` (parameters such as `; charset=utf-8` are fine) | 415 |

To expose it directly instead, set both:

| Variable | Effect |
|----------|--------|
| `TRUSSC_MCP_HOST` | Bind address — e.g. `0.0.0.0` for all interfaces (default `127.0.0.1`) |
| `TRUSSC_MCP_TOKEN` | Bearer token required on every `/mcp` request (`Authorization: Bearer <token>`) |

Binding a non-loopback host **without** `TRUSSC_MCP_TOKEN` is refused
(fail-closed) — the MCP surface can inject input and mutate the scene, so it is
never silently exposed to the network.
