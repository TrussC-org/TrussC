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
    if (imguiBegin()) {
        ImGui::Begin("My Window");
        ImGui::Text("Hello from TrussC!");
        if (ImGui::Button("Click me")) {
            logNotice() << "Button clicked";
        }
        ImGui::End();
    }
    imguiEnd();
}
```

ImGui renders on top of all TrussC content automatically via the `onRender` event.
Teardown is automatic too — the addon listens to the framework's exit event and
shuts itself down when the app closes, so there is nothing to call in `cleanup()`.
With hot reload, the same teardown runs on its App’s `events().hotReloadUnload` before
that generation reloads, and the new build sets ImGui up again in its `setup()`.

## Vertex and index buffers / 頂点・インデックスバッファ / 정점·인덱스 버퍼

### English

`imguiSetup()` starts each context with room for 65,536 vertices and three
times as many indices. Before uploading a frame, both CPU staging blocks and
GPU buffers double until all command lists fit. That frame renders in full.
Capacity belongs to each window or offscreen context, follows its peak, and
does not shrink. The first growth in each context logs one Notice with the
frame's vertex count, previous capacity, and a suggestion to start larger.
Later growth does not repeat the Notice.

```cpp
void tcApp::setup() {
    imguiSetup(200000);  // start with room for 200k vertices; grows automatically if needed
}
```

`ImGuiManager::setup(int initialVertices)` and
`setupOffscreen(int initialVertices)` accept the same initial capacity.
The no-argument forms remain available; values below 1 select the default.
There is no configured upper cap: representable sizes and available memory
limit growth. Indices always have three times the vertex capacity, so an
index-heavy frame can trigger growth before the vertex capacity is reached.
If an allocation failure is detected, the previous buffers remain usable: one Error reports
that part of this frame's UI was not drawn, and the complete command lists
that fit are rendered. The next frame can try again.
Limits: on GL/GLES/WebGL, sokol reports buffer creation as valid even if the driver
runs out of memory, so GPU out-of-memory during growth may go undetected
and drawing may be lost for that frame.
This does not change Dear ImGui's 16-bit index limit for a single primitive.

### 日本語

`imguiSetup()` の初期容量はコンテキストごとに65,536頂点で、インデックスは
その3倍です。各フレームの転送前にCPU作業領域とGPUバッファを倍増し、すべての
描画リストが収まるようにするため、拡張したフレームも全体を描画します。
容量はウィンドウ・オフスクリーンの各コンテキストで独立し、最大使用量に合わせて
増え、縮小しません。最初の拡張時だけNoticeを出し、必要な頂点数、以前の容量、
初期容量を増やす方法を案内します。その後の拡張ではNoticeを繰り返しません。

上の `imguiSetup(200000)` の例で初期容量を20万頂点にできます。
`ImGuiManager::setup(int initialVertices)` と `setupOffscreen(int initialVertices)`
にも同じ値を指定できます。引数なしの呼び出しは従来どおり使え、1未満の値は既定値を
選びます。独自の上限は設けず、サイズの表現可能範囲と空きメモリが限界です。
インデックス容量は常に頂点容量の3倍なので、インデックスが多い描画でも拡張します。
確保の失敗を検知した場合は以前のバッファを保持し、そのフレームのUIの一部を描画できなかった
ことをErrorで1回通知します。収まる描画リストまでは描画し、次のフレームで再試行できます。
GL/GLES/WebGLではドライバがメモリ不足でもsokolがバッファ作成を有効と報告するため、
拡張時のGPUメモリ不足を検知できず、そのフレームの描画が失われる場合があります。
Dear ImGuiの単一プリミティブに対する16ビットインデックスの制限は変わりません。

### 한국어

`imguiSetup()`의 초기 용량은 컨텍스트마다 정점 65,536개이며 인덱스는 그 3배입니다.
매 프레임 업로드 전에 CPU 임시 저장 공간과 GPU 버퍼를 두 배씩 늘려 모든 그리기 목록을
담으므로, 확장한 프레임도 전체 UI를 그립니다. 용량은 창과 오프스크린 컨텍스트마다
독립적이며 최대 사용량을 따라 늘어나고 줄어들지 않습니다. 각 컨텍스트의 첫 확장에만
Notice로 필요한 정점 수, 이전 용량, 초기 용량을 늘리는 방법을 알립니다.
이후 확장에서는 Notice를 반복하지 않습니다.

위의 `imguiSetup(200000)` 예제로 초기 용량을 정점 20만 개로 설정할 수 있습니다.
`ImGuiManager::setup(int initialVertices)`와 `setupOffscreen(int initialVertices)`도
같은 값을 받습니다. 인수 없는 호출은 그대로 사용할 수 있고 1 미만은 기본값을 선택합니다.
별도의 상한은 없으며 표현 가능한 크기와 사용 가능한 메모리가 한계입니다.
인덱스 용량은 항상 정점 용량의 3배이므로 인덱스가 많은 프레임도 확장을 일으킬 수 있습니다.
할당 실패를 감지하면 이전 버퍼를 유지하고 해당 프레임의 UI 일부를 그리지 못했다는 Error를
한 번 기록합니다. 들어가는 완전한 목록까지 그리며 다음 프레임에서 다시 시도할 수 있습니다.
GL/GLES/WebGL에서는 드라이버 메모리가 부족해도 sokol이 버퍼 생성을 유효하다고
보고하므로 확장 중 GPU 메모리 부족을 감지하지 못해 해당 프레임의 그리기가 사라질 수 있습니다.
Dear ImGui의 단일 프리미티브에 대한 16비트 인덱스 제한은 바뀌지 않습니다.

## Offscreen UI

Create a separate `tcx::imgui::ImGuiManager` and call `setupOffscreen()` while
its owning window is current. Keep the manager alive across frames. With an
allocated `fbo` and that `manager`, use:

```cpp
manager.setupOffscreen();
// each frame
fbo.begin(); if (auto ui = manager.scoped()) { ImGui::Text("hi"); } fbo.end();
```

`setupOffscreen()` creates independent UI state without window input hooks or
a shared ini file. Route offscreen input explicitly through the context's
`ImGuiIO`; do not expect window input to be forwarded automatically.

`scoped()` starts a frame only when its boolean result is true. End the scope
before `Fbo::end()`, on the same active target: its destructor ends the frame,
renders synchronously into the Fbo, and restores the previous ImGui context.
The attachment stores premultiplied RGBA for `BlendMode::Alpha` compositing.
A context may begin once per Sokol frame; rendering UI into another target
before present requires a separate manager.

Window `imguiBegin()` / `imguiEnd()` retain deferred `onRender` timing and use
`getDpiScale()` independently of `pixelPerfect`. `imguiBegin()` returns `false`
on refusal. Submit widgets only on success, and pair every attempt, including
a refused one, with `imguiEnd()`:

```cpp
if (imguiBegin()) { ImGui::Text("hi"); }
imguiEnd();
```

## API

### Lifecycle

| Function | Description |
|----------|-------------|
| `imguiSetup()` / `imguiSetup(int initialVertices)` | Initialize ImGui with an optional initial vertex capacity (call in setup) |
| `imguiShutdown()` | Shut down ImGui early (optional — runs automatically on app exit) |
| `imguiBegin()` | Attempt to start a window UI frame; returns false on refusal. Always pair with `imguiEnd()` |
| `imguiEnd()` | End ImGui frame (rendering is deferred to onRender) |

### Owned offscreen manager (`tcx::imgui::ImGuiManager`)

| Method | Description |
|--------|-------------|
| `setupOffscreen()` | Initialize independent offscreen UI state while its owning window is current |
| `scoped()` | Return a scope whose boolean result indicates whether frame start succeeded; destruction ends the frame and restores the previous context. Requires an active Fbo for an offscreen manager |

See [Offscreen UI](#offscreen-ui) for target lifetime, input and compositing rules.

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

After `imguiBegin()` succeeds, use the standard [Dear ImGui API](https://github.com/ocornut/imgui) directly (`ImGui::Begin()`, `ImGui::Button()`, etc). See the [Dear ImGui demo](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp) for a full showcase.

## MCP Tools

When using TrussC's MCP interface, tcxImGui provides tools for AI agents to inspect and interact with ImGui widgets.

Call `imguiSetup()` to register the tools and grant Control in `setup()` to
operate widgets (ReadOnly suffices for queries). The order is independent:

```cpp
void tcApp::setup() {
    imguiSetup();
    mcp::setToolLevel(mcp::ToolLevel::Control);  // Allow widget interaction
}
```

### Available Tools

| Tool | Arguments | Description |
|------|-----------|-------------|
| `tcx_imgui_get_widgets` | `window`, `windowId` (optional) | List the widgets drawn in the last frame — in every window running imgui — with labels, types, positions and, for value widgets, their current values |
| `tcx_imgui_get_touched` | — | The value widgets the user changed by hand since startup (or the last reset), with the current value of their variable. Includes widgets not drawn right now, and the edits recorded by addons such as tcxNodeInspector. Items that change no variable (buttons, menu headers, action menu items) are not listed — see [Touched](#touched-what-the-user-changed-by-hand) |
| `tcx_imgui_reset_touched` | — | Clear that record. No value is changed |
| `tcx_imgui_click` | `label`, `window`, `windowId` (optional) | Click a widget by label. A composite widget (`DragFloat3`, `ColorEdit4`, ...) is an error: set it with `tcx_imgui_input` |
| `tcx_imgui_input` | `label`, `text`, `window`, `windowId` (optional) | Set a widget's value: for widgets that hold a value (and text fields); buttons and other items without a variable are pressed with `tcx_imgui_click`. A value widget gets `text` as JSON in the reported variable form (`SliderAngle`: radians), written into its variable and read back (see [Setting values](#setting-values)); a text field gets `text` typed in |
| `tcx_imgui_checkbox` | `label`, `value`, `window`, `windowId` (optional) | Toggle or set a checkbox |

`window` is the ImGui window (panel) name. `windowId` is the OS window as
the positive native ID returned by `tc_list_windows`. Actions may omit it only
when exactly one Window is open; zero is not an alias. Widget listing may omit
the filter to list every context. Closed Window IDs never select a new Window.

These tools use ImGui's Test Engine hooks plus a small patch to the bundled
Dear ImGui that hands each value widget's variable to tcxImGui. See
[src/imgui/TRUSSC_MODIFICATIONS.md](src/imgui/TRUSSC_MODIFICATIONS.md).

### Widget values

Each value widget in `tcx_imgui_get_widgets` carries `widget`, `valueType` and
`value`, plus `touched` (changed by hand, see below):

```json
{"label": "speed", "window": "Params", "windowId": 101, "type": "input",
 "widget": "slider", "valueType": "float", "value": 0.35, "touched": true, "rect": {...}}
```

| Widget | `widget` | `value` |
|---|---|---|
| `SliderFloat`, `DragInt`, `InputFloat`, `VSliderFloat`, ... | `slider` / `drag` / `input` | A number. `valueType` names the C++ type (`float`, `double`, `int`, `uint`, `int64`, ...). A float comes as the shortest decimal that reads back as the same float (`0.1f` → `0.1`), so it is exact |
| `DragFloat3`, `SliderInt2`, `InputFloat4`, ... | same | An array, `[x, y, z]`, listed under the widget's own label |
| `SliderAngle` | `slider_angle` | Radians — the variable's value — with `"unit": "rad"`, although the widget displays degrees |
| `ColorEdit3/4`, `ColorPicker3/4` | `color` | The variable as it is: `[r, g, b]` or `[r, g, b, a]`, 0-1. `colorSpace` is `"rgb"`, or `"hsv"` with `ImGuiColorEditFlags_InputHSV` (then the values are the raw HSV the variable holds, not converted) |
| `Combo` | `combo` | The selected index. `item` is the text shown; omitted in the frame of a pick, until the next frame reports the new preview |
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
{"label": "position", "window": "Params", "windowId": 101, "status": "ok",
 "widget": "drag", "valueType": "float", "value": [0.5, -1, 2.25]}
```

- `text` is the value as JSON, in the form `tcx_imgui_get_widgets` and
  `tcx_imgui_get_touched` report (the variable's form): a
  number; an array for a composite widget, one element per component
  (`[x, y, z]`; a color `[r, g, b]` or `[r, g, b, a]` as floats 0-1, or the raw
  HSV the variable holds with `colorSpace` `hsv`); `true` / `false` for a
  `Checkbox` or a `bool*` `MenuItem` / `Selectable`; the item index for a
  `Combo` or `ListBox`; for a `RadioButton(label, int* v, v_button)`, that
  button's own value (its `buttonValue`): target the button whose value you
  want. Another value is refused, because in ImGui `true` from a radio button
  means the variable now holds that button's value (`if
  (ImGui::RadioButton("B", &mode, 1)) onB();` must run for 1 only); radians
  for `SliderAngle`. Its display stays in degrees: input `"1.5707964"` for
  a quarter turn (90 degrees). The response includes `"unit": "rad"` and
  the value read back in radians. Non-numeric or non-finite angle text returns
  `{"status": "error", "message": "..."}` without changing the variable.
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
  the inspector's `rotationDeg` in degrees), the reply is still `ok`, with a
  `message` saying so and `value` = what the variable holds.
- Errors, with nothing written: a value of the wrong shape or type (component
  count, not a number, a fraction for an int, out of the C++ type's range); a
  widget that is not drawn in the frame after the call (collapsed header,
  closed or hidden window, no imgui drawn in that frame); a disabled widget
  (inside `BeginDisabled()`, `MenuItem(..., enabled = false)`,
  `ImGuiSelectableFlags_Disabled`) or a read-only one; a `RadioButton` given
  another button's value; an item with no variable that takes no text (a
  button, an action `MenuItem`, `MenuItem(label, shortcut, bool selected)`,
  `RadioButton(label, bool active)`, a plain `Selectable`, a tree node: it has
  no value to set; press it with `tcx_imgui_click`); a widget that runs more than 2 s after the call (a window
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
   {"label": "speed", "window": "Params", "windowId": 101, "widget": "slider",
    "valueType": "float", "value": 0.35, "visible": true},
   {"label": "tint", "window": "Params", "windowId": 101, "widget": "color",
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
- The record starts when `imguiSetup()` registers the MCP tools and is
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
- **Runtime maintenance** (before MCP expiry, independent of drawing) — Answers a
  `tcx_imgui_input` whose check frame did not come in time (see
  [Setting values](#setting-values)), before the MCP server's own timeout

Window UI renders on top and receives input before your app. Owned offscreen
UI uses the explicit input and synchronous rendering described above.

## Local font imports

Font imports validate sfnt structure, compressed data and decoder workspace.
Allocation checks cover TrussC's font-loader storage and shared stb decoder.
ImGui's own atlas, font-object and vector storage remains outside the
allocation-recovery guarantee.

C++ OOM recovery requires Release builds, or iterator debugging off (`_ITERATOR_DEBUG_LEVEL=0` on MSVC). With iterator debugging enabled, even empty string/container construction and moves can allocate a proxy on an error path.

## License

Dear ImGui is licensed under the MIT License. See [imgui/LICENSE.txt](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).

App-owned registrations
-----------------------

Registrations created in an App constructor, callback or owned completion follow that App's lifetime, including lazy addon initialization. Replacing or removing the App retires its listeners and tools while other Apps keep running. Window teardown detaches the App, sends Window exit notifications around `App::exit()`, disconnects audio and waits for active audio callbacks, then cleans up the App before disposing native/GPU resources. Create window content with `window->setApp<Scene>()`; use `destroyApp()` to leave it empty.

App-tied code, including addon state owned by an App, uses `events()`;
process-wide observers use `runtime().events()`. The manager keeps input/render hooks on its addressed Window, settles widget
deadlines from Runtime maintenance before MCP reply expiry even without a
frame, and subscribes to its App’s `events().hotReloadUnload` for generation
teardown. Only Apps in the retiring generation receive that notification;
other Apps keep their ImGui state. The App owner token retires registrations
on swap or close.

Runtime owns the process `hotReloadUnload` subscription and maintenance tick. Per-App hooks belong on that App’s `events().hotReloadUnload`; they retire with its token. Runtime notifies before App ending at the outer main-thread reload safe point. Hot reload supports one Window; opening a second or later Window logs a Warning and behaviour is not guaranteed. Stop App-owned threads, timers and async work in `hotReloadUnload`; reload does not wait for or exclude them. Ending App A does not retire same-generation App B; generation retirement removes generation registrations after its Apps. Old DLL/dylib/.so modules remain pinned for process lifetime, including retained callback/control-block storage. Maintenance runs without relying on a drawing Window.

## MCP edits

MCP edits have no addon limits on pending values, input events or bytes.
The 2-second write deadline and core MCP ingress bounds (64 MiB HTTP body,
sockets and workers) still apply. Allocation failure inside Dear ImGui behaves
as it does for any ImGui frame.
Text edits settle in linear time. For very large text-edit bursts, remaining
growth comes from Dear ImGui's own event queue, including the backward search
in `AddMousePosEvent()`; Dear ImGui is unchanged.

MCP 編集にはアドオン独自の待機値・入力イベント数・バイト数の上限はありません。
2 秒の書き込み期限と core MCP の受付上限（HTTP 本文 64 MiB、ソケット数、
ワーカー数）は引き続き適用されます。Dear ImGui 内のメモリ確保失敗時の動作は、
通常の ImGui フレームと同じです。
テキスト編集の後処理は線形時間です。非常に大量のテキスト編集で残る処理時間の増加は、
`AddMousePosEvent()` の後方検索を含む Dear ImGui 自身のイベントキューによるものです。
Dear ImGui は変更していません。

MCP 편집에는 애드온 자체의 대기 값, 입력 이벤트 수, 바이트 수 상한이 없습니다.
2초 쓰기 기한과 core MCP 수신 상한(HTTP 본문 64 MiB, 소켓 수, 작업자 수)은
계속 적용됩니다. Dear ImGui 내부의 메모리 할당 실패 동작은 일반 ImGui 프레임과 같습니다.
텍스트 편집의 정리는 선형 시간에 처리됩니다. 매우 큰 텍스트 편집 묶음에서 남는
처리 시간 증가는 `AddMousePosEvent()`의 역방향 검색을 포함한 Dear ImGui 자체의
이벤트 큐에서 발생합니다. Dear ImGui는 변경하지 않았습니다.
