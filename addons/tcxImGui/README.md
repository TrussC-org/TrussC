# tcxImGui

[Dear ImGui](https://github.com/ocornut/imgui) integration addon for TrussC.

Provides immediate-mode GUI with full input handling and rendering, connected via TrussC's event system.

## Setup

1. Add `tcxImGui` to your project's `addons.make`:
   ```
   tcxImGui
   ```
   Or use `trusscli add tcxImGui` to add the addon to your project.

2. Include the header and add the namespace:
   ```cpp
   #include <TrussC.h>
   #include <tcxImGui.h>
   using namespace std;
   using namespace tc;
   using namespace tcx;
   ```

## Usage

```cpp
void tcApp::setup() {
    imguiSetup();
}

void tcApp::draw() {
    clear(0.1f, 0.1f, 0.1f);

    // Your TrussC drawing here...

    // ImGui frame
    imguiBegin();
    ImGui::Begin("My Window");
    ImGui::Text("Hello from TrussC!");
    if (ImGui::Button("Click me")) {
        logNotice() << "Button clicked";
    }
    ImGui::End();
    imguiEnd();
}

void tcApp::cleanup() {
    imguiShutdown();
}
```

ImGui renders on top of all TrussC content automatically via the `onRender` event.

## API

### Lifecycle

| Function | Description |
|----------|-------------|
| `imguiSetup()` | Initialize ImGui (call in setup) |
| `imguiShutdown()` | Shutdown ImGui (call in cleanup) |
| `imguiBegin()` | Start ImGui frame (call at start of your GUI code) |
| `imguiEnd()` | End ImGui frame (rendering is deferred to onRender) |

### Input Query

| Function | Description |
|----------|-------------|
| `imguiWantsMouse()` | Returns true if ImGui is capturing mouse input |
| `imguiWantsKeyboard()` | Returns true if ImGui is capturing keyboard input |

Use these to prevent your app from processing input that ImGui is handling:
```cpp
void tcApp::mousePressed(int x, int y, int button) {
    if (imguiWantsMouse()) return;
    // Your mouse handling...
}
```

### Dear ImGui API

After `imguiBegin()`, use the standard [Dear ImGui API](https://github.com/ocornut/imgui) directly (`ImGui::Begin()`, `ImGui::Button()`, etc). See the [Dear ImGui demo](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp) for a full showcase.

## MCP Tools

When using TrussC's MCP interface, tcxImGui provides tools for AI agents to inspect and interact with ImGui widgets.

To enable, call `imguiSetup()` before `mcp::registerDebuggerTools()`:

```cpp
void tcApp::setup() {
    imguiSetup();
    mcp::registerDebuggerTools();  // ImGui tools auto-registered
}
```

### Available Tools

| Tool | Arguments | Description |
|------|-----------|-------------|
| `imgui_get_widgets` | `window` (optional) | List all widgets with labels, types, and positions |
| `imgui_click` | `label`, `window` (optional) | Click a widget by label |
| `imgui_input` | `label`, `text`, `window` (optional) | Type text into an input widget |
| `imgui_checkbox` | `label`, `value` (optional), `window` (optional) | Toggle or set a checkbox |

These tools use ImGui's Test Engine hooks to collect widget info each frame.

## How It Works

tcxImGui connects to TrussC via two core events:

- **`events().rawEvent`** (priority: BeforeApp) — Routes input events to ImGui
- **`events().onRender`** (priority: 1000) — Renders ImGui after all sokol_gl content is flushed

This means ImGui always renders on top and receives input before your app.

## License

Dear ImGui is licensed under the MIT License. See [imgui/LICENSE.txt](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).
