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
│   ├── imgui_widgets.cpp     # Modified (13 value-hook call sites, one line each)
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
changed by hand. Dear ImGui keeps no values — the caller's variable owns them
— and neither test engine hook is ever given the variable. Even upstream's
test engine reads a value back by Ctrl+Click, Ctrl+A, Ctrl+C and parsing the
clipboard (`ItemReadAsScalar` in `imgui_te_context.cpp`). As of the upstream
base (and `master` 2026-09-25, 1.93.0 WIP) there is no hook that passes a
value pointer, hence this patch.

**Changes (one block between `// [TrussC] begin` and `// [TrussC] end`):**
- `enum ImGuiTcValueKind_` — what the reported data is (Drag / Slider /
  SliderAngle / Input / Color / Combo / ComboPreview / Text).
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
  declares that scope object. Inactive (a null context, a no-op destructor)
  unless `GImGui->TestEngineHookItems` is set.
- `ImGuiTcHook_ItemValue()` and `ImGuiTcHook_EditCount()` are declared here
  and implemented in `tcImGuiHooks.h`.

---

## imgui_widgets.cpp

### 3. Value hook call sites

**Purpose:** Declare `IMGUI_TC_ITEM_VALUE(...)` once in each value widget,
where the pointer to the caller's variable and its type are in hand.

**Changes:** 13 inserted lines, each ending in `// [TrussC]`. No upstream line
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
| `ColorPicker4` (`ColorPicker3` calls it) | `g.NextItemData.ClearFlags();` | 0 | Color | `col`, 3 or 4 |

Placement rules:
- A single widget declares the hook after its `ItemAdd()` succeeded, so a
  clipped widget reports nothing.
- A composite widget declares it before `BeginGroup()` / `PushID(label)`. The
  hook then works out the widget's ID from the label once the ID stack is back
  at the entry level, and takes the group's rect and visibility from
  `g.LastItemData`, which is the group at that point. A window that skips
  items returns before any of these lines; for the wrappers without their own
  `SkipItems` check (`SliderAngle`, `Combo`) the hook ignores a window with
  `SkipItems` set.
- `InputTextEx` passes `&buf`, the address of its `buf` variable, rather than
  `buf` itself: a resize callback (`std::string` inputs) repoints `buf` to the
  new allocation (`buf = callback_data.Buf`).

How the hooks behave (in `tcImGuiHooks.h`):
- Parts of a ColorEdit/ColorPicker (`##X`, `##Text`, the `##picker` popup)
  report nothing, since they edit temporaries. The whole widget reports,
  because `g.ColorEditCurrentID` is 0 again when its hook runs.
- The Ctrl+Click text field of a Drag/Slider (`ImGuiInputTextFlags_TempInput`)
  reports nothing either. The Drag/Slider reports the typed value.

Not hooked, on purpose:
- `Checkbox` / `MenuItem` — their state already arrives through `ItemInfo`
  (`ImGuiItemStatusFlags_Checked`).
- `DragFloatRange2` / `DragIntRange2` — two separate pointers. Their `##min` /
  `##max` drags report individually.
- `ListBox`, `RadioButton`.

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
| `imgui/imgui_widgets.cpp` | 13 (one per call site) |
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
  `grep -c "IMGUI_TC_ITEM_VALUE" addons/tcxImGui/src/imgui/imgui_widgets.cpp` = 13.
  For each one, check that the line it follows (table above) still means the
  same thing. A single widget must still declare the hook after `ItemAdd()`
  succeeded. A composite widget must declare it before `BeginGroup()` /
  `PushID(label)`.
- The test engine hook signatures in `imgui_internal.h`
  (`IMGUI_TEST_ENGINE_ITEM_ADD` / `_ITEM_INFO` and the `extern` declarations)
  still match `tcImGuiHooks.h`.
- The internals the hooks read still exist: `ImGuiContext::ColorEditCurrentID`,
  `BeginComboDepth`, `LastItemData`, `TestEngineHookItems`,
  `ImGuiInputTextFlags_TempInput`, `ImGuiColorEditFlags_InputMask_`.
- Read the "Breaking Changes" sections of `docs/CHANGELOG.txt` between the two
  tags.
- Temporarily build with `#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (in
  `imconfig.h`) to flush out obsolete API still used by TrussC code:
  `addons/tcxNodeInspector`, `tools/` (trusscli GUI), and every example whose
  `addons.make` lists tcxImGui.

### 4. After updating

1. Update the **Upstream base** tag/commit at the top of this file.
2. Update the Dear ImGui version in `docs/LICENSE.md`.
3. Build every project that uses imgui (see step 3). Run one with
   `TRUSSC_MCP=1` and check `tcx_imgui_get_widgets`: values present, a
   `DragFloat3` listed under its own label, `tcx_imgui_input` into it shows up
   in `tcx_imgui_get_touched`.
4. Test on all platforms (macOS, Windows D3D11, Linux, Web, iOS, Android).

### Updating sokol_imgui.h

Same 3-way merge, with BASE = upstream `util/sokol_imgui.h` at the sokol base
commit above, THEIRS = the new upstream version, OURS = this copy. Only
update it together with the core sokol headers (the `sokol_gfx.h` API it
calls must match), and record the new base commit in both TRUSSC_MODIFICATIONS
files.
