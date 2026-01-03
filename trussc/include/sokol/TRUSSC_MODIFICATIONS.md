# TrussC Modifications to sokol_app.h

This file documents modifications made to sokol_app.h for TrussC.
When updating sokol_app.h from upstream, these changes need to be re-applied.

Search for `tettou771` or `Modified by` to find all modified sections.

## Modifications

### 1. Skip Present (D3D11 flickering fix)

**Lines:** ~2216, ~3322, ~8286, ~13719

**Purpose:** Add `sapp_skip_present()` function to skip the next present call, fixing D3D11 flickering in event-driven rendering.

**Changes:**
- Added `skip_present` flag to `_sapp_state_t` struct
- Added `sapp_skip_present()` function declaration and implementation
- Modified D3D11 present logic to check the flag

### 2. Keyboard Events on Canvas (Emscripten)

**Lines:** ~7495-7500, ~7530-7533

**Purpose:** Register keyboard events on canvas element instead of window, allowing other page elements (like Monaco editor in TrussSketch) to handle keyboard events independently.

**Changes:**
- Changed `EMSCRIPTEN_EVENT_TARGET_WINDOW` to `_sapp.html5_canvas_selector` for keydown/keyup/keypress callbacks
- Canvas must have `tabindex` attribute to receive focus
- Shell.html includes mousedown handler to focus canvas on click

**Original code:**
```c
emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, true, _sapp_emsc_key_cb);
emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, true, _sapp_emsc_key_cb);
emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, true, _sapp_emsc_key_cb);
```

**Modified code:**
```c
emscripten_set_keydown_callback(_sapp.html5_canvas_selector, 0, true, _sapp_emsc_key_cb);
emscripten_set_keyup_callback(_sapp.html5_canvas_selector, 0, true, _sapp_emsc_key_cb);
emscripten_set_keypress_callback(_sapp.html5_canvas_selector, 0, true, _sapp_emsc_key_cb);
```

## How to Update

1. Download new sokol_app.h from https://github.com/floooh/sokol
2. Search this file for modification locations
3. Re-apply each modification
4. Test on all platforms (especially D3D11 Windows and Emscripten Web)

## Author

All modifications by tettou771
