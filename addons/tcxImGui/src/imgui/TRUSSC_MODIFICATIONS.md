# TrussC Modifications to Dear ImGui

This file documents all modifications made to the bundled Dear ImGui (and to
`sokol_imgui.h`, which lives next to it) for TrussC.
When updating from upstream, these changes need to be re-applied.

Search for `[TrussC]` to find all modified sections.

**Upstream base:** https://github.com/ocornut/imgui tag `v1.92.9b`, commit `f1cc2ae` (2026-07-31) — the latest stable release, not `master` (see the Dear ImGui row in [docs/ROADMAP.md](../../../../docs/ROADMAP.md): "Use stable versions"). Not the docking branch.

---

## Directory Structure

```
addons/tcxImGui/src/
├── imgui/
│   ├── imconfig.h            # Modified (test engine hooks on + value hook)
│   ├── imgui_widgets.cpp     # Modified (19 value-hook call sites + 24 wrapped returns, one line each)
│   ├── imgui.cpp             # Untouched
│   ├── imgui.h               # Untouched
│   ├── imgui_internal.h      # Untouched
│   ├── imgui_draw.cpp        # Untouched
│   ├── imgui_tables.cpp      # Untouched
│   ├── imgui_demo.cpp        # Untouched
│   ├── imstb_*.h             # Untouched
│   └── TRUSSC_MODIFICATIONS.md
├── sokol_imgui.h             # From sokol util/ + TrussC multi-context patch (see below)
├── tcImGuiHooks.h            # TrussC: implements the hooks (not an upstream file)
├── tcImGuiTools.h            # TrussC: MCP tools (not an upstream file)
└── tcxImGui.h                # TrussC: addon API (not an upstream file)
```

Only the files listed as Modified differ from the upstream tag. Everything else
is copied verbatim.

---

## imconfig.h

### 1. Test engine hooks on

**Purpose:** Collect every widget's ID, label, rect and status flags each frame
for the MCP tools (`tcx_imgui_get_widgets`, `tcx_imgui_click`, ...).

**Changes:**
- `#define IMGUI_ENABLE_TEST_ENGINE` (upstream ships it commented out). The
  four extern functions it requires (`ImGuiTestEngineHook_ItemAdd`,
  `ImGuiTestEngineHook_ItemInfo`, `ImGuiTestEngineHook_Log`,
  `ImGuiTestEngine_FindItemDebugLabel`) are implemented in `tcImGuiHooks.h`.
  They run only for contexts whose `TestEngineHookItems` is set, which
  tcxImGui does only while the MCP tools are registered (`TRUSSC_MCP=1`).

### 2. Widget value hook (`IMGUI_TC_ITEM_VALUE`)

**Purpose:** Let the MCP tools report the VALUE of value widgets (sliders,
drags, inputs, colors, combos, text fields), and which widgets the user
changed by hand, and let `tcx_imgui_input` SET a value widget's variable
without simulating clicks and typing. Dear ImGui keeps no values — the caller's variable owns them
— and neither test engine hook is ever given the variable. Even upstream's
test engine reads a value back by Ctrl+Click, Ctrl+A, Ctrl+C and parsing the
clipboard (`ItemReadAsScalar` in `imgui_te_context.cpp`). As of the upstream
base (and `master` 2026-09-25, 1.93.0 WIP) there is no hook that passes a
value pointer, hence this patch.

**Changes (one block between `// [TrussC] begin` and `// [TrussC] end`):**
- `enum ImGuiTcValueKind_` — what the reported data is (Drag / Slider /
  SliderAngle / Input / Color / Combo / ComboPreview / Text / Bool / Radio /
  ListBox / ListBoxBegin).
- `struct ImGuiTcItemValue` — a scope object. Its destructor calls
  `ImGuiTcHook_ItemValue(this)`, so the hook runs when the widget function
  **returns**, through any of its return paths, and sees the final value of
  this frame (after drag, slider and Ctrl+Click text-input edits have been
  applied). It captures the current window at entry, the widget ID (or 0 =
  "the ID of the label in that window", for composite widgets that have no
  item of their own), the label, the data type, a pointer to the data, the
  component count, the widget flags, and `EditCountAtEntry`.
- `EditCountAtEntry` — the hooks count every edit they see. A composite
  widget (`DragFloat3`, `ColorEdit4`, ...) compares the count at return with
  the count at entry, so an edit of any of its parts marks the whole widget as
  edited. `EndGroup()` forwards `ImGuiItemStatusFlags_Edited` only when
  `g.ActiveId` still belongs to the group; a Ctrl+Click text input committed by
  clicking another widget above it would otherwise go unrecorded.
- `IMGUI_TC_ITEM_VALUE(id, label, kind, data_type, data, components, flags)` —
  declares that scope object, then (a second statement, so the call sites
  stay one line each) calls `ImGuiTcHook_ItemEntry(&object)` at the widget's
  **entry**, before the widget reads its variable. The entry hook returns the
  edit count for `EditCountAtEntry`, and writes a value the MCP tools queued
  for this widget through `Data` (the only write through `Data`). Inactive (a
  null context, no entry call, a no-op destructor) unless
  `GImGui->TestEngineHookItems` is set.
- `Injected` and `IMGUI_TC_RETURN(ret)` — the entry hook sets `Injected` when
  it writes a value, and `IMGUI_TC_RETURN(ret)` is `(ret) || Injected`: in
  that frame the widget returns `true`, so code that works on a copy and
  applies it only when the widget returns true (`float x = n->getX(); if
  (ImGui::DragFloat("x", &x)) n->setX(x);`, and inside imgui `CheckboxFlags`
  and `ColorPicker3`) takes the value. Only the return value changes: no
  Edited flag, no `MarkItemEdited()`, so `IsItemEdited()` /
  `IsItemDeactivatedAfterEdit()` do not fire for an injected value and it
  stays out of the touched record.
- `ImGuiTcHook_ItemValue()` and `ImGuiTcHook_ItemEntry()` are declared here
  and implemented in `tcImGuiHooks.h`. (`ImGuiTcHook_ItemEntry()` replaced
  `ImGuiTcHook_EditCount(ctx)` with #321; it takes the whole object, so it
  also knows the widget's ID, kind and data.)

---

## imgui_widgets.cpp

### 3. Value hook call sites

**Purpose:** Declare `IMGUI_TC_ITEM_VALUE(...)` once in each value widget,
where the pointer to the caller's variable and its type are in hand.

**Changes:** 19 inserted lines, and 24 upstream `return` lines wrapped in
`IMGUI_TC_RETURN(...)`, each ending in `// [TrussC]`. No other upstream line
is modified.

| Function | Placed after | Id | Kind | Data |
|---|---|---|---|---|
| `BeginCombo` | `if (!ItemAdd(...)) return false;` | `id` | ComboPreview | `preview_value` |
| `Combo` (getter form; the other forms call it) | `ImGuiContext& g = *GImGui;` | 0 | Combo | `current_item` |
| `DragScalar` | `if (!ItemAdd(...)) return false;` | `id` | Drag | `p_data` |
| `DragScalarN` | `bool value_changed = false;` | 0 | Drag | `p_data`, `components` |
| `SliderScalar` | `if (!ItemAdd(...)) return false;` | `id` | Slider | `p_data` |
| `SliderScalarN` | `bool value_changed = false;` | 0 | Slider | `v`, `components` |
| `SliderAngle` | the opening `{` | 0 | SliderAngle | `v_rad` (radians — the inner `SliderFloat` sees degrees) |
| `VSliderScalar` | `if (!ItemAdd(frame_bb, id)) return false;` | `id` | Slider | `p_data` |
| `InputScalar` | `ImGuiStyle& style = g.Style;` | 0 | Input | `p_data` |
| `InputScalarN` | `bool value_changed = false;` | 0 | Input | `p_data`, `components` |
| `InputTextEx` | `const ImGuiID id = window->GetID(label);` | `id` | Text | `&buf` |
| `ColorEdit4` (`ColorEdit3` calls it) | `g.NextItemData.ClearFlags();` | 0 | Color | `col`, 3 or 4 |
| `ColorPicker4` (`ColorPicker3` calls it) | `const bool is_readonly = ...;` (before `g.NextItemData.ClearFlags();`) | 0 | Color | `col`, 3 or 4 |
| `Checkbox` | `const bool is_visible = ItemAdd(total_bb, id);` (before the clip return) | `id` | Bool | `v` |
| `RadioButton` (`int*` form) | the opening `{` | 0 | Radio | `v`; Flags `v_button` (the button's own value) |
| `Selectable` (`bool*` form) | the opening `{` | 0 | Bool | `p_selected`; Flags `ImGuiItemFlags_Disabled` with `ImGuiSelectableFlags_Disabled` |
| `BeginListBox` | the `IsRectVisible()` early return | `id` | ListBoxBegin | none (`NULL`) |
| `ListBox` (getter form; the array form calls it) | `ImGuiContext& g = *GImGui;` | 0 | ListBox | `current_item` |
| `MenuItem` (`bool*` form) | the opening `{` | 0 | Bool | `p_selected` (may be `NULL`: nothing is reported); Flags `ImGuiItemFlags_Disabled` when `!enabled` |

Placement rules:
- A single widget declares the hook after its `ItemAdd()` succeeded, so a
  clipped widget reports nothing. Two exceptions:
  - `Checkbox` declares it right after `ItemAdd()`, before its clip return:
    its clipped path still reports `ItemInfo` (so the widget stays listed as
    drawn), and the value must follow the variable there too.
  - `BeginListBox` has no `ItemAdd()` of its own (its child window adds the
    item in `EndListBox`); it declares the hook after its `IsRectVisible()`
    early return.
- A composite widget declares it before `BeginGroup()` / `PushID(label)`. The
  hook then works out the widget's ID from the label once the ID stack is back
  at the entry level, and takes the group's rect and visibility from
  `g.LastItemData`, which is the group at that point. A window that skips
  items returns before any of these lines; for the wrappers without their own
  `SkipItems` check (`SliderAngle`, `Combo`, `ListBox`, `RadioButton`,
  `Selectable`, `MenuItem`) the hook ignores a window with `SkipItems` set.
- A wrapper that flips or sets the variable after the inner widget returns
  (`MenuItem(bool*)`, `Selectable(bool*)`, `RadioButton(int*)`) declares it at
  its top, with Id 0: the hook runs after the flip, and the label's ID in the
  entry window is the inner item's ID (`MenuItemEx`'s `Selectable("")` inside
  `PushID(label)` hashes to the pushed ID).
- A Bool hook's Flags are the item flags the widget adds for itself once the
  hook has run (`MenuItem` calls `BeginDisabled()` inside `MenuItemEx` when
  `enabled` is false; `Selectable` disables itself for
  `ImGuiSelectableFlags_Disabled`), so the hook can refuse a disabled one.
- `ColorPicker4` reads a ReadOnly flag set for the next item and then clears
  it; its hook sits before that clear, so the hook sees the flag too.
- `InputTextEx` passes `&buf`, the address of its `buf` variable, rather than
  `buf` itself: a resize callback (`std::string` inputs) repoints `buf` to the
  new allocation (`buf = callback_data.Buf`).

Return rule: in every function whose hook can take a value (all the rows
above except `BeginCombo`, `BeginListBox` and `InputTextEx`), **every
`return` after the `IMGUI_TC_ITEM_VALUE` line** becomes
`return IMGUI_TC_RETURN(<expr>); // [TrussC]`, early returns (a clipped
`Checkbox`, `if (!BeginCombo(...)) return false;`) and `return true;`
included, so the rule has no exceptions. The 24 lines:

| Function | Wrapped returns |
|---|---|
| `Checkbox` | the clipped `return false;`, `return pressed;` |
| `RadioButton` (`int*` form) | `return pressed;` |
| `Combo` (getter form) | `if (!BeginCombo(...)) return false;`, `return value_changed;` |
| `DragScalar` | `return TempInputScalar(...);`, `return value_changed;` |
| `DragScalarN` | `return value_changed;` |
| `SliderScalar` | `return TempInputScalar(...);`, `return value_changed;` |
| `SliderScalarN` | `return value_changed;` |
| `SliderAngle` | `return value_changed;` |
| `VSliderScalar` | `return value_changed;` |
| `InputScalar` | `return ret;` (EnterReturnsTrue), `return value_changed;` |
| `InputScalarN` | `return value_changed;` |
| `ColorEdit4` | `return value_changed;` |
| `ColorPicker4` | `return value_changed;` |
| `Selectable` (`bool*` form) | `return true;`, `return false;` |
| `ListBox` (getter form) | `if (!BeginListBox(...)) return false;`, `return value_changed;` |
| `MenuItem` (`bool*` form) | `return true;`, `return false;` |

How the hooks behave (in `tcImGuiHooks.h`):
- Parts of a ColorEdit/ColorPicker (`##X`, `##Text`, the `##picker` popup)
  report nothing, since they edit temporaries. The whole widget reports,
  because `g.ColorEditCurrentID` is 0 again when its hook runs.
- The Ctrl+Click text field of a Drag/Slider (`ImGuiInputTextFlags_TempInput`)
  reports nothing either. The Drag/Slider reports the typed value.
- Setting a value (`tcx_imgui_input` on a value widget, #321): the tool queues
  the bytes for the widget's ID and the kind it reported. The entry hook of the
  next widget with that ID and kind (`SliderAngle`'s inner `SliderFloat` and
  `Combo`'s `BeginCombo` share the ID, not the kind) writes them through
  `Data`, checking that the data type and component count still match and
  that the widget is not disabled (`ImGuiItemFlags_Disabled` in the current
  or next-item flags, or a Bool hook's Flags) or read-only
  (`ImGuiItemFlags_ReadOnly`, `ImGuiSliderFlags_ReadOnly`,
  `ImGuiInputTextFlags_ReadOnly` on an InputScalar), and, for a `RadioButton`,
  that the value is the button's own `v_button` (its Flags; in ImGui `true`
  from a radio button means the variable holds that button's value). It keeps
  the variable's old bytes and sets `Injected`, so the widget returns true that
  frame, unless the variable already held the value (nothing changed: a toggle
  handler must not run); a mixed-state check box (`ImGuiItemFlags_MixedValue`,
  `CheckboxFlags` with only some of its bits set) holds neither value and gets
  `Injected` for `false` too. The
  return hook of that same call reads the variable back, and the entry hook of
  the widget's next frame checks, before any new write, what the variable
  holds: the value (applied), the old value (a copy that ignores the return
  value and is re-filled every frame, reported as an error), or a third value
  (the app took the value and changed it, e.g. a setter that converts it:
  applied, with a note). The outcome is handed to the tool at the end of the
  frame it is known (`swapFrames()`), or, in a window frame where the app runs
  no imgui for that context, from tcxImGui's render listener
  (`settleWithoutImGuiFrame()`). A value whose next-frame check has not come
  by its check deadline (the window renders no frame) is settled on its
  read-back at return from the main window's afterFrame
  (`settleOverdueValues()`). The write sets no Edited flag, so it is not
  recorded as touched. Text (buffer size unknown) and the openers (`BeginCombo`,
  `BeginListBox`: no variable) are never written. `ColorPicker3` and
  `CheckboxFlags` need no hook of their own: the inner `ColorPicker4` /
  `Checkbox` is written and returns true, and they copy the value out.
- Only the value hook creates "touched" entries, plus the routing in
  `ItemInfo`: a pick inside a list box's child window (its `ChildId` is the
  list box ID, recorded by the `BeginListBox` hook) or inside a combo popup
  (`BeginComboDepth`) goes to that list box / combo, the list box first. So an
  entry carries the value of a caller's variable, except one routed to a
  custom `BeginCombo` / `BeginListBox`, which has no variable: only its label
  (and a combo's item shown). `ItemInfo` also refreshes existing entries.

Not hooked, on purpose:
- `MenuItem(label, shortcut, bool selected)`, `Selectable(label, bool
  selected)`, `RadioButton(label, bool active)`, `BeginMenu` — the caller's
  variable (if any) is never passed in. Their clicks are not recorded as
  touched. (`Checkbox` was left out until #322, because its `Checked` flag
  arrives through `ItemInfo`; it is hooked now so that one rule holds: touched
  entries come from value hooks only.)
- `DragFloatRange2` / `DragIntRange2` — two separate pointers. Their `##min` /
  `##max` drags report individually.

---

## sokol_imgui.h

Not a Dear ImGui file, but it is bundled here, in the addon, not with the core
sokol headers (see `core/include/sokol/TRUSSC_MODIFICATIONS.md`).

**Upstream base:** https://github.com/floooh/sokol `util/sokol_imgui.h` at commit
`082152c` (2026-05-21) — the same commit as the core sokol headers — plus the
cherry-pick below. It must stay in step with the core `sokol_gfx.h`: upstream
`sokol_imgui.h` after `082152c` moves to `sg_write_buffer_transient()`, which
the bundled `sokol_gfx.h` does not have yet.

### 4. Multi-context API (`simgui_tc_*`)

**Purpose:** One independent sokol_imgui instance (own ImGui context, font
atlas, buffers, pipeline) per window, for multi-window apps.

**Changes (marked `[TrussC]`):**
- Public declarations: `simgui_tc_context`, `simgui_tc_make_context()`,
  `simgui_tc_set_context()`, `simgui_tc_get_context()`,
  `simgui_tc_destroy_context()`.
- `_simgui_state_t` gains `imgui_ctx`, and `_simgui` becomes a macro for the
  ACTIVE instance (`*_simgui_cur`), so the rest of the implementation compiles
  unchanged. The default instance keeps classic single-window behavior.
- `_simgui_imgui_create_context()` captures the context `CreateContext()`
  returns and makes it current (since 1.90 `CreateContext()` restores the
  previous context before returning).
- The implementation of the five functions at the end of the file.

### 5. Upstream cherry-pick `544c0ef` (for Dear ImGui 1.92.9)

`draw_data->CmdListsCount` → `draw_data->CmdLists.Size` (3 lines; 1.92.9
marked `CmdListsCount` obsolete). Identical to upstream, so it carries no
marker and merges cleanly on the next update.

---

## Grep markers

```bash
grep -rn "\[TrussC" addons/tcxImGui/src/imgui/ addons/tcxImGui/src/sokol_imgui.h
```

Expected counts (lines containing the marker; this file excluded):

| File | Lines |
|---|---|
| `imgui/imconfig.h` | 4 (the hooks-on line, `begin`, a comment, `end`) |
| `imgui/imgui_widgets.cpp` | 43 (19 call sites + 24 wrapped returns) |
| `sokol_imgui.h` | 5 |

---

## How to Update Dear ImGui

Patched files (`imconfig.h`, `imgui_widgets.cpp`) are updated with a 3-way
merge, not by overwriting and hand-copying the patches. That avoids slips when
copying patches by hand. Every other file is overwritten from the new tag.

### 1. Stage the three versions

```bash
git clone https://github.com/ocornut/imgui.git /tmp/imgui
OLD=v1.92.9b                 # the upstream base recorded above
NEW=<new stable tag>
for f in imconfig.h imgui_widgets.cpp; do
  git -C /tmp/imgui show $OLD:$f > /tmp/BASE_$f      # upstream, pre-patch
  git -C /tmp/imgui show $NEW:$f > /tmp/THEIRS_$f    # upstream, target
  cp addons/tcxImGui/src/imgui/$f /tmp/OURS_$f       # current TrussC copy
  git merge-file -p /tmp/OURS_$f /tmp/BASE_$f /tmp/THEIRS_$f > /tmp/MERGED_$f
done
```

`git merge-file` exits with the number of conflicts. Resolve the
`<<<<<<<` / `=======` / `>>>>>>>` blocks, then copy the merged files back.
(`--diff-algorithm=histogram` is optional; git 2.43 rejects it, so drop it
there.)

### 2. Overwrite the untouched files

```bash
for f in imgui.cpp imgui.h imgui_internal.h imgui_draw.cpp imgui_tables.cpp \
         imgui_demo.cpp imstb_rectpack.h imstb_textedit.h imstb_truetype.h; do
  git -C /tmp/imgui show $NEW:$f > addons/tcxImGui/src/imgui/$f
done
```

### 3. Check what the patches rely on

- Every hook is still in place and still in the right spot:
  `grep -c "IMGUI_TC_ITEM_VALUE" addons/tcxImGui/src/imgui/imgui_widgets.cpp` = 19.
  For each one, check that the line it follows (table above) still means the
  same thing. A single widget must still declare the hook after `ItemAdd()`
  succeeded. A composite widget must declare it before `BeginGroup()` /
  `PushID(label)`.
- Every `return` after a settable widget's hook is still wrapped (return rule
  above): `grep -c "IMGUI_TC_RETURN" addons/tcxImGui/src/imgui/imgui_widgets.cpp`
  = 24, and no plain `return` was added after those hooks by the new version
  (a new early return must be wrapped too).
- The test engine hook signatures in `imgui_internal.h`
  (`IMGUI_TEST_ENGINE_ITEM_ADD` / `_ITEM_INFO` and the `extern` declarations)
  still match `tcImGuiHooks.h`.
- The internals the hooks read still exist: `ImGuiContext::ColorEditCurrentID`,
  `BeginComboDepth`, `LastItemData`, `TestEngineHookItems`,
  `ImGuiInputTextFlags_TempInput`, `ImGuiColorEditFlags_InputMask_`,
  `ImGuiWindow::ChildId` (and `BeginListBox` still opening a child window with
  the list box ID).
- Read the "Breaking Changes" sections of `docs/CHANGELOG.txt` between the two
  tags.
- Temporarily build with `#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (in
  `imconfig.h`) to flush out obsolete API still used by TrussC code:
  `addons/tcxNodeInspector`, `tools/` (trusscli GUI), and every example whose
  `addons.make` lists tcxImGui.

### 4. After updating

1. Update the **Upstream base** tag/commit at the top of this file.
2. Update the Dear ImGui version in `docs/LICENSE.md`.
3. Build and run the headless tests in `addons/tcxImGui/tests/` (they cover
   the touched record through real clicks — menus, check boxes, radio buttons,
   list boxes — and setting values through the hook, read back per
   component). Build every project that uses imgui (see step 3). Run one with
   `TRUSSC_MCP=1` and check `tcx_imgui_get_widgets`: values present, a
   `DragFloat3` listed under its own label, `tcx_imgui_input` with
   `[1, 2, 3]` into it returns ok and `tcx_imgui_get_widgets` then shows
   exactly `[1, 2, 3]` (every component, not only one), and it does not show
   up in `tcx_imgui_get_touched`.
4. Test on all platforms (macOS, Windows D3D11, Linux, Web, iOS, Android).

### Updating sokol_imgui.h

Same 3-way merge, with BASE = upstream `util/sokol_imgui.h` at the sokol base
commit above, THEIRS = the new upstream version, OURS = this copy. Only
update it together with the core sokol headers (the `sokol_gfx.h` API it
calls must match), and record the new base commit in both TRUSSC_MODIFICATIONS
files.
