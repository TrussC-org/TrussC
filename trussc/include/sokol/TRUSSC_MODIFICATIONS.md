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

### 3. 10-bit Color Output (Metal / D3D11)

**Files:** sokol_app.h (~line 1846, ~4972, ~6089, ~8125, ~8253, ~8282, ~13522), sokol_glue.h (~line 149)

**Purpose:** Use RGB10A2 (10-bit per channel, 2-bit alpha) instead of BGRA8 (8-bit per channel) for the default framebuffer on Metal and D3D11. This reduces color banding on 10-bit displays (MacBook Pro, iMac, Pro Display XDR, most modern Windows monitors). Same 32-bit bandwidth as BGRA8, so no performance impact. WebGL is unchanged (does not support 10-bit).

**Changes:**
- Added `SAPP_PIXELFORMAT_RGB10A2` to `sapp_pixel_format` enum
- macOS Metal: `MTLPixelFormatBGRA8Unorm` → `MTLPixelFormatBGR10A2Unorm`
- iOS Metal: `MTLPixelFormatBGRA8Unorm` → `MTLPixelFormatBGR10A2Unorm`
- D3D11 swap chain: `DXGI_FORMAT_B8G8R8A8_UNORM` → `DXGI_FORMAT_R10G10B10A2_UNORM`
- D3D11 MSAA texture: `DXGI_FORMAT_B8G8R8A8_UNORM` → `DXGI_FORMAT_R10G10B10A2_UNORM`
- D3D11 resize buffer: `DXGI_FORMAT_B8G8R8A8_UNORM` → `DXGI_FORMAT_R10G10B10A2_UNORM`
- `sapp_color_format()` returns `SAPP_PIXELFORMAT_RGB10A2` for Metal/D3D11
- `_sglue_to_sgpixelformat()` maps `SAPP_PIXELFORMAT_RGB10A2` → `SG_PIXELFORMAT_RGB10A2`

**Note:** Alpha precision is reduced from 8-bit to 2-bit (4 levels). This only affects the final swapchain output; offscreen FBOs are unaffected. Transparent window compositing may be limited but typically not needed for creative coding.

## How to Update

1. Download new sokol_app.h from https://github.com/floooh/sokol
2. Search this file for modification locations
3. Re-apply each modification
4. Test on all platforms (especially D3D11 Windows and Emscripten Web)

## Author

All modifications by tettou771
