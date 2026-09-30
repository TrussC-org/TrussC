# tcxImGui

[Dear ImGui](https://github.com/ocornut/imgui) integration addon for TrussC.

Provides immediate-mode GUI with full input handling and rendering, connected via TrussC's event system.

## Setup

1. Add `tcxImGui` to your project's `addons.make`:
   ```
   tcxImGui
   ```
   Or use `trusscli addon add tcxImGui` to add the addon to your project.

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
```

ImGui renders on top of all TrussC content automatically via the `onRender` event.
Teardown is automatic too — the addon listens to the framework's exit event and
shuts itself down when the app closes, so there is nothing to call in `cleanup()`.

## API

### Lifecycle

| Function | Description |
|----------|-------------|
| `imguiSetup()` | Initialize ImGui (call in setup) |
| `imguiShutdown()` | Shut down ImGui early (optional — runs automatically on app exit) |
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

To enable, call `imguiSetup()` before `mcp::registerControlTools()`:

```cpp
void tcApp::setup() {
    imguiSetup();
    mcp::registerControlTools();  // ImGui tools auto-registered
}
```

### Available Tools

| Tool | Arguments | Description |
|------|-----------|-------------|
| `tcx_imgui_get_widgets` | `window`, `windowId` (optional) | List the widgets drawn in the last frame — in every window running imgui — with labels, types, positions and, for value widgets, their current values |
| `tcx_imgui_get_touched` | — | The value widgets the user changed by hand since startup (or the last reset), with the current value of their variable. Includes widgets not drawn right now, and the edits recorded by addons such as tcxNodeInspector. Items that change no variable (buttons, menu headers, action menu items) are not listed — see [Touched](#touched-what-the-user-changed-by-hand) |
| `tcx_imgui_reset_touched` | — | Clear that record. No value is changed |
| `tcx_imgui_click` | `label`, `window`, `windowId` (optional) | Click a widget by label. A composite widget (`DragFloat3`, `ColorEdit4`, ...) is an error: set it with `tcx_imgui_input` |
| `tcx_imgui_input` | `label`, `text`, `window`, `windowId` (optional) | Set a widget's value. A value widget gets `text` as JSON, written into its variable and read back (see [Setting values](#setting-values)); a text field gets `text` typed in |
| `tcx_imgui_checkbox` | `label`, `value`, `window`, `windowId` (optional) | Toggle or set a checkbox |

`window` is the ImGui window (panel) name. `windowId` is the OS window as
`tc_list_windows` numbers it (0 = main); you only need it when the same panel
exists in two OS windows.

These tools use ImGui's Test Engine hooks plus a small patch to the bundled
Dear ImGui that hands each value widget's variable to tcxImGui. See
[src/imgui/TRUSSC_MODIFICATIONS.md](src/imgui/TRUSSC_MODIFICATIONS.md).

### Widget values

Each value widget in `tcx_imgui_get_widgets` carries `widget`, `valueType` and
`value`, plus `touched` (changed by hand, see below):

```json
{"label": "speed", "window": "Params", "windowId": 0, "type": "input",
 "widget": "slider", "valueType": "float", "value": 0.35, "touched": true, "rect": {...}}
```

| Widget | `widget` | `value` |
|---|---|---|
| `SliderFloat`, `DragInt`, `InputFloat`, `VSliderFloat`, ... | `slider` / `drag` / `input` | A number. `valueType` names the C++ type (`float`, `double`, `int`, `uint`, `int64`, ...). A float comes as the shortest decimal that reads back as the same float (`0.1f` → `0.1`), so it is exact |
| `DragFloat3`, `SliderInt2`, `InputFloat4`, ... | same | An array, `[x, y, z]`, listed under the widget's own label |
| `SliderAngle` | `slider_angle` | Radians — the variable's value — with `"unit": "rad"`, although the widget displays degrees |
| `ColorEdit3/4`, `ColorPicker3/4` | `color` | The variable as it is: `[r, g, b]` or `[r, g, b, a]`, 0-1. `colorSpace` is `"rgb"`, or `"hsv"` with `ImGuiColorEditFlags_InputHSV` (then the values are the raw HSV the variable holds, not converted) |
| `Combo` | `combo` | The selected index. `item` is the text shown |
| `BeginCombo` (a custom combo) | `combo` | Only `item`, the text shown |
| `InputText`, `InputTextMultiline` | `text` | The string. A password field reports `"password": true` and never its text |
| `Checkbox`, `MenuItem(label, shortcut, bool* p_selected)`, `Selectable(label, bool* p_selected)` | `checkbox` | `true` / `false`, the variable after the click (a `Checkbox` or toggle `MenuItem` also has `checked`) |
| `RadioButton(label, int* v, int v_button)` | `radio` | The variable the button group sets (`valueType` `int`), under the label of each button in the group. `buttonValue` is that button's own value (`v_button`), the value pressing it sets |
| `ListBox` | `listbox` | The selected index, under the list box's own label |
| `BeginListBox` (a custom list box) | `listbox` | No value, only the label |

Items that set no variable of yours carry no value: buttons, menu headers,
action menu items (`MenuItem("Save")`), `MenuItem(label, shortcut, bool
selected)` (the variable is yours, the widget never sees it), plain
`Selectable`s and `RadioButton(label, bool active)`.

The parts of a composite widget (the `##X` / `##Y` fields of a `ColorEdit`, the
`-` / `+` buttons of `InputInt`) are still listed, so you can click or type
into them, but carry no value — the widget reports under its own label.

### Setting values

`tcx_imgui_input` on a value widget (every row of the table above except the
custom `BeginCombo` / `BeginListBox` and text fields) does not click or type.
It hands the value to the value hook, which writes it into the app's variable
the next time the widget runs, before the widget reads it. In that frame the
widget returns `true`, as if the user had changed it, so the usual patterns
take the value:

```cpp
if (ImGui::DragFloat3("pos", v)) recompute();            // recompute() runs once
float x = node->getX();
if (ImGui::DragFloat("x", &x)) node->setX(x);            // setX() gets the value
```

The reply waits for that frame and the next one: the variable is read back
when the widget returns, and checked again when the widget runs in the next
frame.

```json
{"label": "position", "window": "Params", "windowId": 0, "status": "ok",
 "widget": "drag", "valueType": "float", "value": [0.5, -1, 2.25]}
```

- `text` is the value as JSON, in the units `tcx_imgui_get_widgets` reports: a
  number; an array for a composite widget, one element per component
  (`[x, y, z]`; a color `[r, g, b]` or `[r, g, b, a]` as floats 0-1, or the raw
  HSV the variable holds with `colorSpace` `hsv`); `true` / `false` for a
  `Checkbox` or a `bool*` `MenuItem` / `Selectable`; the item index for a
  `Combo` or `ListBox`; for a `RadioButton(label, int* v, v_button)`, that
  button's own value (its `buttonValue`): target the button whose value you
  want. Another value is refused, because in ImGui `true` from a radio button
  means the variable now holds that button's value (`if
  (ImGui::RadioButton("B", &mode, 1)) onB();` must run for 1 only); radians
  for `SliderAngle`.
- A value the variable already holds changes nothing: the widget does not
  return `true` (a toggle handler such as `if (ImGui::MenuItem("Fullscreen",
  nullptr, &fs)) toggleFullscreen();` does not run), and the reply is `ok`. A
  `CheckboxFlags` with only some of its bits set (drawn mixed, reported
  `false`) holds neither value, so `false` clears its bits and `true` sets
  them.
- `status: ok` means the variable held the value when the widget returned, and
  still held it when the widget ran in the next frame (if the widget is not
  drawn in that next frame, or the app draws no imgui in it — say the value
  hid the GUI — or its window renders no frame until 4 s after the call — a
  window rendering less often than once every 2 s, or one that stopped — the
  first read-back stands). If the app took the value and
  changed it by the next frame (a clamp, a setter that converts it, such as
  the inspector's `rotation` in degrees), the reply is still `ok`, with a
  `message` saying so and `value` = what the variable holds.
- Errors, with nothing written: a value of the wrong shape or type (component
  count, not a number, a fraction for an int, out of the C++ type's range); a
  widget that is not drawn in the frame after the call (collapsed header,
  closed or hidden window, no imgui drawn in that frame); a disabled widget
  (inside `BeginDisabled()`, `MenuItem(..., enabled = false)`,
  `ImGuiSelectableFlags_Disabled`) or a read-only one; a `RadioButton` given
  another button's value; an item with no variable that takes no text (a
  button, an action `MenuItem`, `MenuItem(label, shortcut, bool selected)`,
  `RadioButton(label, bool active)`, a plain `Selectable`: use
  `tcx_imgui_click`); a widget that runs more than 2 s after the call (a window
  rendering less often than once every 2 s, e.g. `Window::setFps` below 0.5),
  since the value could not be checked before the reply.
- Errors after the write, carrying what the variable holds: a hand edit that
  changed the value again in the same frame; and a variable that holds its old
  value again in the next frame — code that ignores the widget's return value
  and copies its own value into the variable every frame (`float y = model.y;
  ImGui::DragFloat("y", &y);`), so such a widget cannot be set from MCP (or an
  app that turned the value back itself).
- A value set this way is not an edit by hand: the Edited flag is not set, so
  `ImGui::IsItemEdited()` and `IsItemDeactivatedAfterEdit()` do not fire for
  it, and it does not go into `tcx_imgui_get_touched` (tcxNodeInspector's
  record included). Only the return value says `true`.

Known limits:

- No range clamp: the hook does not see the widget's min / max, so a value out
  of the slider's range is written as given.
- Text fields (`InputText`) are still typed into (the hook does not see the
  buffer size), and `tcx_imgui_click` still clicks buttons.

### Touched: what the user changed by hand

A typical loop: you tweak sliders in the running app, then ask the AI to "make
it like this". The AI calls `tcx_imgui_get_touched`, writes those values into
the code, and calls `tcx_imgui_reset_touched`.

```json
{"status": "ok", "count": 2,
 "widgets": [
   {"label": "speed", "window": "Params", "windowId": 0, "widget": "slider",
    "valueType": "float", "value": 0.35, "visible": true},
   {"label": "tint", "window": "Params", "windowId": 0, "widget": "color",
    "valueType": "color", "value": [1, 0.4, 0.2, 1], "colorSpace": "rgb", "visible": false}
 ],
 "inspector": []}
```

- A value widget (every row of the table above) is recorded when its value is
  changed through the widget: dragging, typing, clicking — including a click by
  `tcx_imgui_click` and text typed by `tcx_imgui_input`. A value assigned from
  code, or set on a value widget by `tcx_imgui_input` (see
  [Setting values](#setting-values)), is never recorded; a recorded widget's
  value does follow later changes from code.
- Only value widgets are recorded, each with the value of its variable. A click
  that sets no variable of yours is not: buttons, menu headers, action menu
  items, `MenuItem(label, shortcut, bool selected)` — even when your code uses
  it as a toggle, since the widget never sees your variable (use the `bool*`
  form to have it recorded) — plain `Selectable`s and `RadioButton(label, bool
  active)`.
- Changing a part (one component of a `DragFloat3`, the R field of a
  `ColorEdit`) records the whole widget under its label. Likewise a pick in a
  `Combo` or a `ListBox` records the combo / list box under its own label, not
  the item picked. A custom list (`BeginListBox` + `Selectable`s) is recorded
  under the list box label with no value, like a custom `BeginCombo`. A value
  widget inside a custom list box or combo (a `Selectable(label, bool*)`, a
  `Checkbox`) is recorded under its own label too, with its value.
- A toggle `MenuItem` / `Selectable` with a `bool*` reports the state after the
  click, even when the click closed its menu.
- Each `RadioButton(label, int* v, v_button)` the user pressed gets its own
  entry, and all of them show the same variable's current value (each with its
  own `buttonValue`).
- A widget that is not drawn right now (collapsed header, closed window) keeps
  its last known value and reports `"visible": false`.
- The record starts when the MCP tools are registered (`TRUSSC_MCP=1`) and is
  kept until `tcx_imgui_reset_touched`.
- Other keys come from addons that keep their own record. tcxNodeInspector adds
  `inspector`: the node members edited in its panel or moved with its gizmo,
  per node. Its Hierarchy / Inspector panels are left out of `widgets`, because
  their widgets are shared by whichever node is selected.

An addon can contribute its own list with
`tcx::imgui::addTouchedSource(key, get, reset)`, and keep its panels out of the
widget record with a `tcx::imgui::TouchedExclusionScope` while it draws them.

## How It Works

tcxImGui connects to TrussC via these core events:

- **`events().rawEvent`** (priority: BeforeApp) — Routes input events to ImGui
- **`events().onRender`** (priority: 1000) — Renders ImGui after all sokol_gl content is flushed
- **the main window's `events().afterFrame`** (priority: BeforeApp) — Answers a
  `tcx_imgui_input` whose check frame did not come in time (see
  [Setting values](#setting-values)), before the MCP server's own timeout

This means ImGui always renders on top and receives input before your app.

## License

Dear ImGui is licensed under the MIT License. See [imgui/LICENSE.txt](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).
