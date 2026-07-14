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
2. **Inspection tools** (`get_screenshot`, `save_screenshot`) are automatically registered.
3. The server endpoint URL is printed to stderr: `[MCP] HTTP server listening on http://localhost:PORT/mcp`

### Related: `TRUSSC_LOG_FILE`

Independent of MCP mode, setting `TRUSSC_LOG_FILE=/path/to/app.log` makes the app
call `setLogFile()` before `setup()` runs, so every log line — including
setup-time output — is appended to that file with zero app code. This is how a
supervisor process (e.g. `trusscli kiosk`) captures logs from an unmodified app.

## Transport

TrussC uses **HTTP transport** for MCP. All JSON-RPC messages are sent as HTTP POST requests to the `/mcp` endpoint.

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC request → response |
| OPTIONS | `/mcp` | CORS preflight (204) |
| GET | `/` | Server info (for port discovery) |

## Standard MCP Tools

### Inspection Tools (always available in MCP mode)
| Tool | Arguments | Description |
|------|-----------|-------------|
| `get_screenshot` | (none) | Get current screen as Base64 PNG image (full resolution, lossless — encodes synchronously on the frame loop, so use it for one-off debugging captures, not polling) |
| `save_screenshot` | `path` | Save screenshot to file |
| `get_thumbnail` | `width`, `quality` (both optional) | Small Base64 JPEG for monitoring/periodic polling (default width 512, aspect preserved, never upscales; quality default 75). Only the framebuffer readback touches the frame loop — downscale + JPEG encode run on the HTTP worker thread, so polling this does not stutter the app (measured under continuous hammering: ~179 fps vs ~46 fps with `get_screenshot`) |
| `get_health` | (none) | Lightweight liveness snapshot: `{fps, frameCount, uptimeSec, width, height, version, memoryBytes}`. Reads counters only (no GPU state), so it is cheap enough for a supervisor to poll |
| `quit` | (none) | Quit the application gracefully |
| `get_node_tree` | `id`, `depth` (both optional) | Dump the node tree (or a subtree) as JSON: per node `{type, name, id, members, mods, children}`. Members are the `TC_REFLECT`ed values — rotation as euler degrees, colors as `[r,g,b,a]` floats 0-1, Vec3 as `[x,y,z]`, enums as their label string. `mods` lists each attached Mod as `{type, members}`. `depth` limits recursion (~270 bytes/node — on large scenes, explore with `depth` + drill into subtrees by `id`; cut-off nodes carry a `childCount`) |
| `get_selected_node` | (none) | The currently selected node (same shape, no children), or `null` |

### Recording Tools (always available in MCP mode)

The video counterpart of `save_screenshot` — capture the window to a video file with the native encoder (no ffmpeg). Always registered when MCP is enabled, but unlike the inspection tools these *write* (start/stop a recording), so they are listed separately.

| Tool | Arguments | Description |
|------|-----------|-------------|
| `start_recording` | `path`, `duration`, `fps`, `codec` (all optional) | Start recording the window. Omit `path` for a timestamped `recording-<timestamp>.mp4` (`.mov` for ProRes) in the data dir; relative paths resolve under the data dir. `duration` (seconds) makes a fixed-length clip that **auto-stops and finalizes itself** at exactly that length (omit or `0` = unlimited). `fps` is the target frame rate (default 60; ProMotion frames are decimated to it). `codec` is `h264` (default) / `hevc` / `prores422` / `prores4444`. Returns `{status, path (resolved), fps, codec}` plus `duration` when a fixed length was set |
| `stop_recording` | (none) | Stop the current recording and finalize the file. A manual stop **always wins** over a pending fixed `duration` — it finalizes immediately at the current length. Returns `{status, recording:false, path, frames, length}` (`length` = measured output seconds). Idle is not an error: returns `{status:"ok", recording:false, message:"not recording"}` |

### Debugger Tools (opt-in via `mcp::registerDebuggerTools()`)
| Tool | Arguments | Description |
|------|-----------|-------------|
| `mouse_move` | `x`, `y`, `button` (optional) | Move cursor; with `button` held, emits a drag |
| `mouse_press` | `x`, `y`, `button` | Press and hold — start of a drag gesture |
| `mouse_release` | `x`, `y`, `button` | Release — end of a drag gesture |
| `mouse_click` | `x`, `y`, `button`, `shift`/`ctrl`/`alt`/`super` (optional) | Click mouse button (0:left, 1:right), optionally with modifier keys held — e.g. `super: true` Cmd+clicks |
| `mouse_scroll` | `dx`, `dy` | Scroll mouse wheel |
| `key_press` | `key` | Press a key (sokol_app keycode) |
| `key_release` | `key` | Release a key |
| `select_node` | `id` | Select a node by instance id (0 clears); drives the same selection an inspector shows |
| `set_node_members` | `id`, `members`, `mod` (optional) | Set reflected members from a JSON object (same encoding as `get_node_tree`; enums accept label string or int). Pass `mod` (a Mod's short type name, e.g. `"LayoutMod"`) to target a mod attached to the node instead of the node itself. Reports `applied` / `skipped` (type mismatch) / `readOnly` / `unknown` keys |

The node tools make a scene **round-trippable for agents**: arrange things in a
GUI (e.g. with the `tcxNodeInspector` addon's gizmo), then `get_node_tree` to
read the exact values back and bake them into code — or drive the scene the
other way with `set_node_members`.

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
  -d '{"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"save_screenshot","arguments":{"path":"/tmp/test.png"}}}'

# Mouse click (requires registerDebuggerTools())
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"mouse_click","arguments":{"x":100,"y":200}}}'

# Record a fixed 3-second clip (auto-stops & finalizes itself); omit "duration"
# for an unlimited recording you end with stop_recording. Omit "path" for a
# timestamped file in the data dir; the response carries the resolved path.
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":4,"params":{"name":"start_recording","arguments":{"path":"/tmp/clip.mp4","duration":3}}}'

# Stop early (a manual stop always wins — valid shorter file); no-op if idle
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":5,"params":{"name":"stop_recording","arguments":{}}}'
```

### Taking Screenshots from Shell

**IMPORTANT for AI agents**: Do NOT use macOS `screencapture`, OS screen recorders (QuickTime, `ffmpeg`-avfoundation, OBS, etc.), or similar OS commands. TrussC apps may render with Metal/OpenGL and the OS cannot capture the screen correctly. Always use the MCP tools — `save_screenshot` for a still, `start_recording` / `stop_recording` for video.

```bash
# Start app, wait, take screenshot, then kill
TRUSSC_MCP=1 TRUSSC_MCP_PORT=8080 ./bin/myApp.app/Contents/MacOS/myApp &
sleep 2
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{}}'
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"save_screenshot","arguments":{"path":"/tmp/screenshot.png"}}}'
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
| Inspection (read-only) | `get_screenshot`, `save_screenshot`, `get_thumbnail`, `get_health`, `get_node_tree`, `get_selected_node` | Automatic when MCP is enabled |
| Recording (window capture to video) | `start_recording`, `stop_recording` | Automatic when MCP is enabled |
| Debugger (input injection / scene mutation) | `mouse_click`, `mouse_press`, `mouse_release`, `key_press`, `mouse_move`, `mouse_scroll`, `key_release`, `select_node`, `set_node_members` | `mcp::registerDebuggerTools()` |
| ImGui (widget interaction) | `imgui_get_widgets`, `imgui_click`, `imgui_input`, `imgui_checkbox` | Requires tcxImGui addon + `mcp::registerDebuggerTools()` |
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
