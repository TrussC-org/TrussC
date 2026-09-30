# tcxImGui tests

Headless console test (no window, no GPU). It runs a real Dear ImGui context
with the bundled, patched imgui and the hooks from `src/tcImGuiHooks.h`, and
checks what the MCP tools would report.

`src/imguiHarness.h` is the driver, meant to be shared by every tcxImGui test:

- an ImGui context with no renderer: `DisplaySize` set, the font atlas built on
  the CPU (the "backend" claims `RendererHasTextures` and never uploads), no
  `imgui.ini`;
- `frame()` runs one frame the way tcxImGui does (`beginFrame()` → your UI →
  `swapFrames()` → `Render()`), with widget collection on; with
  `setImGuiWhen(pred)`, a frame where `pred()` is false runs no imgui and
  settles the queued values as tcxImGui's render listener does; with
  `setRenderWhen(pred)`, a frame where `pred()` is false renders nothing (a
  window that stopped rendering);
- `click(label)` clicks the centre of a widget found by label in the last
  frame's registry, feeding mouse events through `ImGuiIO` one per frame;
- `touched(label)` / `touchedJson(label)` read the touched record, the latter
  in the JSON shape `tcx_imgui_get_touched` prints;
- `callTool(h, name, args)` calls an MCP tool as the server's frame-start
  queue does, and for a tool that defers its reply to after the next frame,
  runs frames and, after each, the main window's afterFrame work (tcxImGui's
  settling of overdue values, then the core's drain) until the reply is built.

Cases (`src/main.cpp`):

- touched record (#322): a toggle `MenuItem(bool*)` in a dropdown is recorded
  with its new value although the menu closes; `Selectable(bool*)` and
  `Checkbox` carry their value; `RadioButton(int*)` reports the variable under
  the pressed button; a `ListBox` pick is recorded under the list box label
  with the index, a custom `BeginListBox` list under its label; menu headers,
  action menu items, `MenuItem(bool selected)` toggles, plain `Selectable`s and
  `RadioButton(bool)` are not recorded; a `Checkbox` scrolled out of view still
  reports its variable; a custom list box inside a combo popup owns its picks.
- setting values (#321): `tcx_imgui_input` writes a `DragFloat2` (whose
  centre is the gap between its fields), `DragFloat3`, `ColorEdit4`,
  `SliderAngle`, `Checkbox`, `Combo`, `InputInt`, `RadioButton`, `ListBox`,
  `MenuItem(bool*)` and `Selectable(bool*)` through the value hook, and each
  variable is checked; wrong shapes and types, a widget not drawn in the next
  frame and a same-frame hand edit are errors; injected values stay out of the
  touched record; `tcx_imgui_click` on a composite is an error; text fields are
  still typed into. The widget returns true in the write frame: a getter/setter
  copy, `CheckboxFlags` and `ColorPicker3` take the value, `if (DragFloat(...))
  ++count;` counts once and `IsItemEdited()` stays false; a clipped `Checkbox`
  is written; a copy that ignores the return value gets the verify error;
  disabled and read-only widgets and items with no variable (an action
  `MenuItem`, `RadioButton(label, bool)`, a button) are refused, and not
  pressed.
- the return value of every settable widget: each one used as a copy applied
  only on `true` (`DragFloat3`, `SliderFloat(2)`, `SliderAngle`,
  `VSliderFloat`, `InputInt` with and without step, `EnterReturnsTrue`,
  `InputInt2`, `ColorEdit4`, `ColorPicker4`, `Combo` closed and open,
  `ListBox` visible and clipped, `RadioButton(int*)`, `Selectable(bool*)`,
  `MenuItem(bool*)`, a clipped `Checkbox`, a Drag / Slider in Ctrl+Click text
  mode) takes the value.
- refusals and what the app does next: a disabled `MenuItem(bool*)` /
  `Selectable(bool*)`, `ImGuiSliderFlags_ReadOnly`,
  `PushItemFlag(ImGuiItemFlags_ReadOnly)` and a `ColorPicker4` with the next
  item's ReadOnly flag are refused; a clamp or conversion after the return is
  `ok` with the value held, a clamp back to the old value is the revert error.
- frames without imgui: a value that hides the GUI is answered after two
  frames, a call while it is hidden after one, and the value it dropped is
  not written when the widget runs again; a widget that runs past the value's
  lifetime is not written; a window that renders no frame after the write is
  answered `ok` on the read-back at the check deadline (the clock is advanced
  through `detail::clockSkew`), not by the core's timeout.
- `RadioButton(int*)`: `tcx_imgui_get_widgets` lists each button's
  `buttonValue`; the button whose value it is takes the value and its handler
  runs once; another button's value is refused at once (and by the value hook
  when the button's value changed since it was listed), nothing written.
- the value the variable already holds: a toggle `MenuItem`, a drag and a
  selected radio button run no handler, and the reply is `ok`; a mixed
  `CheckboxFlags` still takes `false` (bits cleared) and `true`.
- tcxNodeInspector's `ImGuiReflector` (a test dependency, see `addons.make`):
  an injected value is applied but not recorded as an edit; a click is.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
