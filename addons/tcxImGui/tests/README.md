# tcxImGui tests

Headless console test (no window, no GPU). It runs a real Dear ImGui context
with the bundled, patched imgui and the hooks from `src/tcImGuiHooks.h`, and
checks what the MCP tools would report.

`src/imguiHarness.h` is the driver, meant to be shared by every tcxImGui test:

- an ImGui context with no renderer: `DisplaySize` set, the font atlas built on
  the CPU (the "backend" claims `RendererHasTextures` and never uploads), no
  `imgui.ini`;
- `frame()` runs one frame the way tcxImGui does (`beginFrame()` → your UI →
  `swapFrames()` → `Render()`), with widget collection on;
- `click(label)` clicks the centre of a widget found by label in the last
  frame's registry, feeding mouse events through `ImGuiIO` one per frame;
- `touched(label)` / `touchedJson(label)` read the touched record, the latter
  in the JSON shape `tcx_imgui_get_touched` prints;
- `callTool(h, name, args)` calls an MCP tool as the server's frame-start
  queue does, and for a tool that defers its reply to after the next frame,
  runs that frame and builds the reply as the after-present drain does.

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
  still typed into.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
