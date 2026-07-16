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
# Auto-assign port (printed to stderr on startup)
TRUSSC_MCP=1 ./myApp

# Or specify a port
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8080 ./myApp
```

When enabled:
1. An **HTTP server** starts on the specified port (or an OS-assigned port).
2. **Inspection tools** (`tc_get_screenshot`, `tc_save_screenshot`) are automatically registered.
3. The server endpoint URL is printed to stderr: `[MCP] HTTP server listening on http://localhost:PORT/mcp`

### Related: `TRUSSC_LOG_FILE`

Independent of MCP mode, setting `TRUSSC_LOG_FILE=/path/to/app.log` makes the app
call `setLogFile()` before `setup()` runs, so every log line — including
setup-time output — is appended to that file with zero app code. This is how a
supervisor process (e.g. `anchorbolt start`) captures logs from an unmodified app.

## Transport

TrussC uses **HTTP transport** for MCP. All JSON-RPC messages are sent as HTTP POST requests to the `/mcp` endpoint.

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC request → response |
| OPTIONS | `/mcp` | CORS preflight (204) |
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
| `tc_get_screenshot` | `format`, `width`, `quality`, `window` (all optional) | Screenshot as an MCP image content block (rendered inline by MCP clients) plus a text metadata block. Defaults to full-resolution lossless PNG; pass `width` for a downscaled monitoring thumbnail (aspect preserved, never upscales, clamped 16-4096) and `format: "jpg"` (+ `quality`, default 75) for small payloads. `window` = index from `tc_list_windows` (default 0 = main). Cheap to poll at any settings: only the framebuffer readback touches the frame loop — downscale + encode run on the HTTP worker thread (measured under continuous hammering at jpg/512: ~179 fps vs ~46 fps for the old synchronous encode; baseline ~236) |
| `tc_save_screenshot` | `path`, `window`? | Save screenshot to file. Optional `window` index from `tc_list_windows` (default 0 = main) |
| `tc_list_windows` | (none) | List open windows: index 0 = main, then secondary windows (title, size). Use the index as the `window` arg above |
| `tc_get_health` | (none) | Lightweight liveness snapshot: `{fps, frameCount, uptimeSec, width, height, version, pid, rssBytes, memoryBytes}`. Reads counters only (no GPU state), so it is cheap enough for a supervisor to poll. `pid` lets a supervisor confirm the reply comes from *its* child (port collisions); `rssBytes` is whole-process resident memory (the leak-hunting number); `memoryBytes` is sokol-tracked allocations only |
| `tc_get_status` | (none) | App-published ops status (see [Publishing custom ops status](#publishing-custom-ops-status)): `{values: [{name, value, mode}], images: [names]}`. `mode` is `"status"` (show as-is) or `"graph"` (plot over time). Empty when the app publishes nothing |
| `tc_get_status_image` | `name`, `width`, `quality` (last two optional) | Fetch an app-published image registered via `mcp::statusImage()`, downscaled + JPEG-encoded exactly like `tc_get_screenshot` (pixel grab on the main loop, encode on the HTTP worker — no frame stutter) |
| `tc_get_alerts` | - | Drain operator alerts raised via `mcp::alert()` — returns and clears the pending list, so exactly one consumer receives each alert |
| `tc_quit` | (none) | Quit the application gracefully |
| `tc_get_node_tree` | `id`, `depth` (both optional) | Dump the node tree (or a subtree) as JSON: per node `{type, name, id, members, mods, children}`. Members are the `TC_REFLECT`ed values — rotation as euler degrees, colors as `[r,g,b,a]` floats 0-1, Vec3 as `[x,y,z]`, enums as their label string. `mods` lists each attached Mod as `{type, members}`. `depth` limits recursion (~270 bytes/node — on large scenes, explore with `depth` + drill into subtrees by `id`; cut-off nodes carry a `childCount`) |
| `tc_get_selected_node` | (none) | The currently selected node (same shape, no children), or `null` |

### Recording Tools (always available in MCP mode)

The video counterpart of `tc_save_screenshot` — capture the window to a video file with the native encoder (no ffmpeg). Always registered when MCP is enabled, but unlike the inspection tools these *write* (start/stop a recording), so they are listed separately.

| Tool | Arguments | Description |
|------|-----------|-------------|
| `tc_start_recording` | `path`, `duration`, `fps`, `codec` (all optional) | Start recording the window. Omit `path` for a timestamped `recording-<timestamp>.mp4` (`.mov` for ProRes) in the data dir; relative paths resolve under the data dir. `duration` (seconds) makes a fixed-length clip that **auto-stops and finalizes itself** at exactly that length (omit or `0` = unlimited). `fps` is the target frame rate (default 60; ProMotion frames are decimated to it). `codec` is `h264` (default) / `hevc` / `prores422` / `prores4444`. Returns `{status, path (resolved), fps, codec}` plus `duration` when a fixed length was set |
| `tc_stop_recording` | (none) | Stop the current recording and finalize the file. A manual stop **always wins** over a pending fixed `duration` — it finalizes immediately at the current length. Returns `{status, recording:false, path, frames, length}` (`length` = measured output seconds). Idle is not an error: returns `{status:"ok", recording:false, message:"not recording"}` |

### Debugger Tools (opt-in via `mcp::registerDebuggerTools()`)
| Tool | Arguments | Description |
|------|-----------|-------------|
| `tc_mouse_move` | `x`, `y`, `button` (optional) | Move cursor; with `button` held, emits a drag |
| `tc_mouse_press` | `x`, `y`, `button` | Press and hold — start of a drag gesture |
| `tc_mouse_release` | `x`, `y`, `button` | Release — end of a drag gesture |
| `tc_mouse_click` | `x`, `y`, `button`, `shift`/`ctrl`/`alt`/`super` (optional) | Click mouse button (0:left, 1:right), optionally with modifier keys held — e.g. `super: true` Cmd+clicks |
| `tc_mouse_scroll` | `dx`, `dy` | Scroll mouse wheel |
| `tc_key_press` | `key` | Press a key (sokol_app keycode) |
| `tc_key_release` | `key` | Release a key |
| `tc_select_node` | `id` | Select a node by instance id (0 clears); drives the same selection an inspector shows |
| `tc_set_node_members` | `id`, `members`, `mod` (optional) | Set reflected members from a JSON object (same encoding as `tc_get_node_tree`; enums accept label string or int). Pass `mod` (a Mod's short type name, e.g. `"LayoutMod"`) to target a mod attached to the node instead of the node itself. Reports `applied` / `skipped` (type mismatch) / `readOnly` / `unknown` keys |

The node tools make a scene **round-trippable for agents**: arrange things in a
GUI (e.g. with the `tcxNodeInspector` addon's gizmo), then `tc_get_node_tree` to
read the exact values back and bake them into code — or drive the scene the
other way with `tc_set_node_members`.

To enable debugger tools, call `mcp::registerDebuggerTools()` in your `setup()`.
Registering the tools *is* the opt-in — there is no separate enable step, and
`mcp::isDebuggerEnabled()` will report `true` once they are registered:

```cpp
void tcApp::setup() {
    mcp::registerDebuggerTools();
}
```

These tools are inert unless the MCP server is also running (`TRUSSC_MCP=1`).

### ImGui Tools (requires tcxImGui addon)

The **tcxImGui** addon provides additional MCP tools for AI agents to inspect and interact with ImGui widgets. To use these, add `tcxImGui` to your project via `trusscli add tcxImGui` or `addons.make`, then call `imguiSetup()` before `mcp::registerDebuggerTools()`.

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
curl -X POST http://localhost:8080/mcp \
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
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{}}'

# Take screenshot
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"tc_save_screenshot","arguments":{"path":"/tmp/test.png"}}}'

# Mouse click (requires registerDebuggerTools())
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"tc_mouse_click","arguments":{"x":100,"y":200}}}'

# Record a fixed 3-second clip (auto-stops & finalizes itself); omit "duration"
# for an unlimited recording you end with tc_stop_recording. Omit "path" for a
# timestamped file in the data dir; the response carries the resolved path.
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":4,"params":{"name":"tc_start_recording","arguments":{"path":"/tmp/clip.mp4","duration":3}}}'

# Stop early (a manual stop always wins — valid shorter file); no-op if idle
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":5,"params":{"name":"tc_stop_recording","arguments":{}}}'
```

### Taking Screenshots from Shell

**IMPORTANT for AI agents**: Do NOT use macOS `screencapture`, OS screen recorders (QuickTime, `ffmpeg`-avfoundation, OBS, etc.), or similar OS commands. TrussC apps may render with Metal/OpenGL and the OS cannot capture the screen correctly. Always use the MCP tools — `tc_save_screenshot` for a still, `tc_start_recording` / `tc_stop_recording` for video.

```bash
# Start app, wait, take screenshot, then kill
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8080 ./bin/myApp.app/Contents/MacOS/myApp &
sleep 2
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{}}'
curl -s -X POST http://localhost:8080/mcp \
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
      "url": "http://localhost:8080/mcp"
    }
  }
}
```

### Port Discovery

- If `TRUSSC_MCP_PORT` is set, the app uses that port.
- If not set (or set to `0`), the OS assigns an available port.
- The actual port is printed to stderr on startup: `[MCP] HTTP server listening on http://localhost:PORT/mcp`
- From code: `mcp::getHttpPort()` returns the actual port number.

## Security Model

| Category | Tools | Enabled by |
|----------|-------|------------|
| Inspection (read-only) | `tc_get_screenshot`, `tc_save_screenshot`, `tc_get_health`, `tc_get_node_tree`, `tc_get_selected_node` | Automatic when MCP is enabled |
| Recording (window capture to video) | `tc_start_recording`, `tc_stop_recording` | Automatic when MCP is enabled |
| Debugger (input injection / scene mutation) | `tc_mouse_click`, `tc_mouse_press`, `tc_mouse_release`, `tc_key_press`, `tc_mouse_move`, `tc_mouse_scroll`, `tc_key_release`, `tc_select_node`, `tc_set_node_members` | `mcp::registerDebuggerTools()` |
| ImGui (widget interaction) | `tcx_imgui_get_widgets`, `tcx_imgui_click`, `tcx_imgui_input`, `tcx_imgui_checkbox` | Requires tcxImGui addon + `mcp::registerDebuggerTools()` |
| Custom | `mcp::tool(...)` | Your code |

### Network exposure

By default the MCP server binds to **localhost only** and sends no CORS headers,
so it is reachable only by native MCP clients on the same machine (a wildcard
CORS origin would otherwise let any web page in your browser drive it). For
remote access, SSH tunnelling is the simplest safe option.

To expose it directly instead, set both:

| Variable | Effect |
|----------|--------|
| `TRUSSC_MCP_HOST` | Bind address — e.g. `0.0.0.0` for all interfaces (default `localhost`) |
| `TRUSSC_MCP_TOKEN` | Bearer token required on every `/mcp` request (`Authorization: Bearer <token>`) |

Binding a non-loopback host **without** `TRUSSC_MCP_TOKEN` is refused
(fail-closed) — the MCP surface can inject input and mutate the scene, so it is
never silently exposed to the network.
