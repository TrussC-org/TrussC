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
  in the JSON shape `tcx_imgui_get_touched` prints.

Cases (`src/main.cpp`):

- touched record (#322): a toggle `MenuItem(bool*)` in a dropdown is recorded
  with its new value although the menu closes; `Selectable(bool*)` and
  `Checkbox` carry their value; `RadioButton(int*)` reports the variable under
  the pressed button; a `ListBox` pick is recorded under the list box label
  with the index, a custom `BeginListBox` list under its label; menu headers,
  action menu items, `MenuItem(bool selected)` toggles, plain `Selectable`s and
  `RadioButton(bool)` are not recorded.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
