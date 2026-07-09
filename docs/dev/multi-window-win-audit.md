# Multi-window Windows driver — cannibalization audit

Audit of `origin/feat/multi-window-win` as **reference source** for a new Windows
secondary-window driver to be written into `core/include/sokol/sokol_app_tc.h`.
Not a merge — pieces are lifted/adapted/replaced individually.

All file:line references below are into the branch file
`core/platform/win/tcWindowWin.cpp` (708 lines) unless noted. Read via
`git show origin/feat/multi-window-win:<path>`.

## Scope of the branch

`git diff $(git merge-base HEAD origin/feat/multi-window-win)..origin/feat/multi-window-win --stat`:

```
 core/include/tc/app/tcWindow.h    |  25 +-   (comments + TC_PLATFORMS("macos,windows") + stub #if guard)
 core/platform/win/tcWindowWin.cpp | 708 ++++  (the whole driver — new file)
 docs/reference/api-reference.toml |  12 +-   (doc strings macOS -> macOS/Windows)
```

Key facts up front:

- **Does NOT touch `sokol_app.h`, `sokol_impl.cpp`, or any sokol win TU.** It is a
  standalone C++ file that plugs into the existing TrussC `Window`/`WindowContext`
  abstraction and borrows sokol_app's already-created D3D11 device via
  `sg_d3d11_device()` / `sg_d3d11_device_context()`. The new design integrates
  into `sokol_app_tc.h` itself (a C header), so all of this C++/TrussC glue must be
  re-expressed against the new per-window frame model.
- **No per-window dt story.** `tickWindow()` calls the *global* `beginFrame()`
  (line 286) and global `present()` (295). There is no per-window EMA frame timer.
  The new `sokol_app_tc.h` already has a per-window EMA timer (header line ~206),
  so dt must come from there, not from this branch.
- **Only two "not-done" markers** (`git grep -i todo/fixme/hack` is clean):
  - line 273: "Same lifecycle caveats as Fbo contexts on sgl resize" (advisory).
  - **line 436: keycode table is a known gap** — see Event handling below.

---

## 1. Window creation — `createWindow()` (637-704), `ensureWindowClass()` (552-564)

- **Window class** (555-562): `WNDCLASSEXW`, class name `L"TrussCSecondaryWindow"`
  (45), style `CS_HREDRAW | CS_VREDRAW | CS_OWNDC`, `IDC_ARROW` cursor, registered
  once via a `static bool` guard (553). Straightforward.
- **Styles** (654): `WS_OVERLAPPEDWINDOW` if `settings.decorated` else `WS_POPUP`.
- **DPI handling** (669-677): creates at the requested logical size interpreted as
  96 DPI (`CW_USEDEFAULT` position), then reads the real per-monitor DPI with
  `GetDpiForWindow`, computes `scale`, and resizes the CLIENT area to
  `logical * scale` using `AdjustWindowRectExForDpi` + `SetWindowPos`. Assumes
  sokol_app has already set per-monitor-v2 awareness (comment 656-657). Clean and
  correct; the two-step create-then-resize is the standard per-monitor-v2 dance.
- `HWND` create at 658-661 stashes `nw` via `lpCreateParams`; retrieved in
  `WM_NCCREATE` (460-463) into `GWLP_USERDATA`.

**Rating: LIFT (logic) / ADAPT (packaging).** The Win32 sequence is correct and
directly reusable, but it lives inside a C++ `Window`/`shared_ptr` factory. In the
C header it becomes a `_sapp_tc` window-slot init function. Class registration,
style selection, and the DPI create-then-resize block port almost verbatim.

---

## 2. Swapchain — `createSwapchain()` (181-219), `createRenderTargets()` (108-156), `resizeSwapchain()` (166-179), `acquireSecondarySwapchain()` (74-91)

- **Factory acquisition** (186-192): `device->QueryInterface(IDXGIDevice)` →
  `GetAdapter` → `adapter->GetParent(IDXGIFactory2)`. Reaches the *same* factory
  that made sokol_app's device — correct approach, avoids a second `CreateDXGIFactory`.
- **Swapchain desc** (194-204): `DXGI_SWAP_CHAIN_DESC1`, format
  `DXGI_FORMAT_R10G10B10A2_UNORM` (10-bit, matches the patched sokol_app main
  window, const at 41), `SampleDesc.Count = 1` (flip model is always single-sample),
  `BufferUsage = RENDER_TARGET_OUTPUT`, **`BufferCount = 2`**,
  **`SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD`**, `Scaling = STRETCH`,
  `AlphaMode = IGNORE`. `CreateSwapChainForHwnd` (206).
- **Alt-Enter suppression** (207): `factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)`
  right after create — the correct flip-model gotcha handling.
- **MSAA approach** (133-143): flip-model swapchains can't be multisampled, so when
  `sampleCount > 1` it creates a *separate* MSAA color texture
  (`D3D11_STANDARD_MULTISAMPLE_PATTERN`) and hands sokol_gfx both a `render_view`
  (MSAA) and `resolve_view` (backbuffer) via `sg_swapchain.d3d11` (83-88). Correct
  and matches how sokol resolves.
- **Depth-stencil** (146-152): `D24_UNORM_S8_UINT`, drawable-sized, sample count
  matched to color. Recreated on resize.
- **Backbuffer RTV** (114-121): `GetBuffer(0)` → `CreateRenderTargetView`. Comment
  notes flip model keeps buffer 0 as the current back buffer every frame so the
  view is reusable (persistent, not per-frame) — this is why
  `acquireSecondarySwapchain` (74-91) is a trivial "hand back current views + size"
  with no per-frame drawable acquisition (contrast Metal). Good insight to preserve.
- **Resize** (166-179): releases all views, then the critical flip-model gotcha
  (170-175): **before `ResizeBuffers` you must release EVERY reference to the back
  buffers, including the RTV still bound on sokol's shared immediate context** —
  so it does `dc->OMSetRenderTargets(0, nullptr, nullptr); dc->Flush();` first.
  Then `ResizeBuffers(0, w, h, kSwapFormat, 0)` and recreate targets. This
  ordering is the single most valuable encoded gotcha in the file.

**Rating:**
- Factory acquisition, desc, flip-discard/BufferCount=2, MakeWindowAssociation,
  MSAA-side-texture + resolve wiring, depth-stencil, ResizeBuffers unbind/flush
  ordering: **LIFT** — all correct D3D11, portable to C almost as-is.
- `BufferCount = 2` and swapchain-flags: **ADAPT.** The new design uses **DXGI
  frame-latency waitable objects**, which requires
  `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` on create, usually
  `BufferCount = 2` or 3 with `SetMaximumFrameLatency(1)`, and passing that same
  flag to every `ResizeBuffers` call (a common bug source: flag dropped on resize
  → waitable handle invalidated). This branch passes `0` flags — must change.
- `acquireSecondarySwapchain` hook: **REPLACE** — it targets TrussC's
  `WindowContext::acquireSwapchain` callback (C++), which does not exist in the
  C-header world. The new driver returns/fills `sg_swapchain` directly at frame time.

---

## 3. Tick model — `tickWindow()` (224-312), `SetTimer` (696-701), `monitorRefreshHz()` (94-105)

- **Driver: `WM_TIMER`.** `SetTimer(hwnd, kTickTimerId, interval, nullptr)` at
  699-701 with `interval = 1000 / monitorRefreshHz(hwnd)` (integer ms). Fires
  `WM_TIMER` (469-470) → `tickWindow`. Delivered on the main thread by sokol_app's
  own message pump; the comment (696-698) leans on Windows auto-coalescing timers
  to depth 1.
- **THIS IS THE PART BEING REPLACED.** `WM_TIMER` resolution is ~15.6 ms
  (`USER_TIMER_MINIMUM` 10 ms, and the timer floor is the system tick), so it
  effectively caps at ~64 Hz and jitters badly — it cannot hit 120/144 Hz displays
  and is not vsync-locked. Integer `1000/hz` (699) also quantizes (e.g. 144 Hz →
  6 ms → 166 Hz nominal, then clamped by the timer floor). The whole point of the
  redesign is to drive each window off a **DXGI frame-latency waitable object**
  (`GetFrameLatencyWaitableObject` → `WaitForSingleObjectEx`) so the tick is
  vsync-paced and jitter-free.
- **Integration with the main loop** (295, 303-304): renders the tree, then
  `Present(0, 0)` with **SyncInterval 0** so the present never blocks the main
  thread on this window's vblank (comment 300-303: a blocking wait would halve the
  main window's rate). This "don't block on secondary present" principle is exactly
  what the waitable-object model formalizes.
- **`monitorRefreshHz`** (94-105): `MonitorFromWindow` → `GetMonitorInfoW` →
  `EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS).dmDisplayFrequency`, fallback 60.
  Still useful as a *fallback* dt estimate, but not as the pacing source.

**Rating: REPLACE (the pacing) / keep the shape.** The per-window-tick-on-main-thread
*structure* (each window renders synchronously, present non-blocking) is exactly
what the new design keeps and what `sokol_app_tc.h` already assumes. But the
`SetTimer`/`WM_TIMER` mechanism is thrown out and replaced by the frame-latency
waitable object polled from the main loop. `monitorRefreshHz` survives only as a
fallback constant.

---

## 4. Event handling — `wndProc()` (458-550) + input helpers (317-453)

Messages handled:

- Mouse buttons L/R/M down+up (473-478) → `mouseButtonEvent` (330-365): tracks a
  `buttonMask`, `SetCapture`/`ReleaseCapture` on first-press/last-release (346, 355)
  so drags survive leaving the client area. Maps to TrussC `MouseEventArgs` +
  fires `events().mousePressed`, `app_->mousePressed`, and
  `dispatchMousePressToTree`.
- `WM_MOUSEMOVE` (480-482) → `mouseMoveEvent` (367-409): arms `TrackMouseEvent`
  (TME_LEAVE) once (372-377), computes delta, emits drag vs move depending on
  `buttonMask`.
- `WM_MOUSELEAVE` (484-493): disarms tracking, parks cursor at (-1,-1) so the next
  tick's `updateHoverState` clears hover.
- `WM_MOUSEWHEEL` (495-498) → `mouseScrollEvent` (411-428): converts screen→client,
  `wheel = WHEEL_DELTA_WPARAM / WHEEL_DELTA`.
- `WM_KEYDOWN/SYSKEYDOWN/KEYUP/SYSKEYUP` (500-509) → `keyEvent` (430-453). SYS
  variants `break` to `DefWindowProc` so Alt/F10 system handling survives (503, 508).
  Repeat flag from `lParam bit 30` (502).
- `WM_SIZE` (511-526): non-minimized → `resizeSwapchain` + re-read DPI +
  `syncRootSize` (fires `windowResized`).
- `WM_DPICHANGED` (528-537): adopts the suggested rect (per-monitor v2); next tick
  picks up the new scale.
- `WM_CLOSE` (539-544): calls `owner->close()` (which deletes `nw` — comment warns
  touch nothing after).
- `WM_DESTROY` (546-547): no-op.

**Modifier keys** (`fillMods`, 317-322): `GetKeyState` for Shift/Ctrl/Alt/LWIN|RWIN.
**DPI→logical conversion** (`logicalPos`, 325-328): `px / (dpi/96)`.

- **KEYCODE GAP (line 430-437, explicit).** `keyEvent` does `int key = vk;` — it
  passes the raw **Win32 virtual-key code straight through as the TrussC/sokol
  keycode**. The comment admits this only works because letters/digits happen to
  share ASCII == VK == SAPP_KEYCODE; **the full `SAPP_KEYCODE` translation table is
  deferred as "Phase 2 polish"**. So arrows, F-keys, punctuation, keypad, etc. are
  wrong. This is the direct analogue of the gap macOS had. The new driver should
  port sokol_app.h's existing `_sapp_win32_key()` VK→`SAPP_KEYCODE` table rather
  than copy this shortcut. No text/char (`WM_CHAR`) handling either.

**Rating:**
- Mouse (buttons/capture/move/drag/leave/wheel), DPI conversion, SYS-key
  pass-through, WM_SIZE/WM_DPICHANGED handling: **ADAPT** — correct behavior, but
  every handler writes into TrussC `WindowContext`/`CoreEvents`/Node-tree dispatch
  (`internal::currentWindowCtx`, `win.events()...`, `win.dispatchMousePressToTree`).
  In the C header these become `sapp_event` fills dispatched through the event
  callback. The *message set* and the capture/track/leave logic transfer; the
  *payload target* is rewritten.
- Keycode mapping: **REPLACE** — use sokol_app.h's VK→SAPP_KEYCODE table; add
  `WM_CHAR` for text.

---

## 5. Occlusion / minimize — `tickWindow()` (229-242, 304-311)

- **Minimize** (231): `if (IsIconic(hwnd)) return;` — timer keeps firing, resumes
  on restore. Fully idle while minimized.
- **Occlusion** (235-242, 304-311): after `Present(0,0)`, if it returns
  `DXGI_STATUS_OCCLUDED` (305) sets `nw->occluded`; subsequent ticks cheaply poll
  with `Present(0, DXGI_PRESENT_TEST)` (236) and skip all rendering until it
  returns `S_OK`. Logs once each way. This is the D3D11 analogue of the mac
  occlusion-skip and is genuinely good — a fully covered window costs one
  present-test per tick, never stalls others.

**Rating: LIFT (logic).** `IsIconic` skip and the `DXGI_PRESENT_TEST` occlusion
poll are the correct idioms and port directly. Note under the waitable-object
model you must still not *block* on the waitable for an occluded/minimized window
(or you'd stall) — keep the "test cheaply, skip render" path.

---

## 6. How it plugs into TrussC — the C++ seam that must be re-expressed

- Renders through TrussC's `Window` / `internal::WindowContext`:
  `internal::currentWindowCtx` swap (269-270, 299), a per-window `sgl_context`
  created lazily on first tick (274-282) with matching color/depth/sample formats,
  `sgl_set_context` + `sgl_defaults` (283-284), global `beginFrame()` (286),
  `win.events().update/draw/afterFrame` + `win.tickTree()` + `win.drawTreeNow()`
  (288-293), then global `present()` (295) which does `sg_end_pass`+`sg_commit`.
- Swapchain handed to sokol via TrussC's `WindowContext::acquireSwapchain`
  function-pointer hook (688-689) → `acquireSecondarySwapchain` (74-91).
- Teardown (`close()`, 583-614): `KillTimer`, clear `GWLP_USERDATA`,
  `DestroyWindow`, release views + swapchain, `sgl_destroy_context`, then App
  `exit()/cleanup()` and unregister from `attachedApps`.

**Rating: REPLACE / ADAPT.** All of this is TrussC-C++-abstraction plumbing that
does not exist in `sokol_app_tc.h`. The new driver owns the window slot in the C
header and calls back out. What transfers conceptually: the per-window sgl context
lifecycle, format agreement across swapchain/sgl/MSAA/depth, and the teardown
ordering (kill tick source → clear userdata → destroy window → release GPU → free
slot). The `beginFrame()`/`present()` being *global* (single frame counter, single
pass bracket) is a limitation to fix in the new model — each window needs its own
frame timing (the header's EMA timer) and its own pass.

---

## 7. D3D11 gotchas encoded in the file (the real value)

1. **Flip-model `ResizeBuffers` ordering** (170-175): unbind RTV from the shared
   immediate context (`OMSetRenderTargets(0,nullptr,nullptr)`) + `Flush()` before
   `ResizeBuffers`, or it fails and the next present crashes. **Highest-value comment.**
2. **Flip-model + MSAA** (133-143, 83-88): swapchain is single-sample; MSAA is a
   side texture that sokol resolves into the backbuffer via `resolve_view`.
3. **Alt-Enter suppression** (207): `MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)`.
4. **Persistent backbuffer RTV** (112-113): flip model keeps buffer 0 current, so
   the RTV is reusable frame-to-frame (no per-frame acquire).
5. **Non-blocking secondary present** (300-304): `Present(0,0)` SyncInterval 0 so a
   secondary never gates the main window's frame rate.
6. **Shared device/context**: uses `sg_d3d11_device()` /
   `sg_d3d11_device_context()` — one device, one immediate context, single-threaded
   (174 comment). Any per-window work runs on that shared context; ordering matters.

All six are LIFT-grade knowledge to carry into the new driver.

---

## Rating summary

| Piece | Rating | Note |
|---|---|---|
| Window class + style + per-monitor-v2 create/resize | LIFT logic / ADAPT packaging | port into C window-slot init |
| DXGI factory acquisition from sokol device | LIFT | verbatim |
| Swapchain desc (flip-discard, 10-bit, MakeWindowAssociation) | LIFT + **ADAPT** | must add FRAME_LATENCY_WAITABLE flag + carry flag through ResizeBuffers |
| MSAA side-texture + resolve wiring | LIFT | correct |
| Depth-stencil creation | LIFT | correct |
| ResizeBuffers unbind/flush ordering | **LIFT (keep verbatim)** | top gotcha |
| `acquireSecondarySwapchain` hook | REPLACE | fill `sg_swapchain` directly, no C++ callback |
| **WM_TIMER / SetTimer pacing** | **REPLACE** | → DXGI frame-latency waitable object |
| `monitorRefreshHz` | keep as fallback | not the pacing source |
| Mouse handlers (capture/track/leave/wheel/drag) | ADAPT | retarget payload to `sapp_event` |
| **Keycode `int key = vk`** | **REPLACE** | use sokol_app VK→SAPP_KEYCODE table; add WM_CHAR |
| Occlusion (PRESENT_TEST) + IsIconic skip | LIFT logic | keep non-blocking skip under waitable model |
| WindowContext/sgl/beginFrame/present plumbing | REPLACE/ADAPT | re-express against C header + per-window timer |
| Teardown ordering | ADAPT | same ordering, C-header resources |

---

## Cross-checks

- **Touches `sokol_app.h` / sokol win TU?** No. Standalone file, borrows the device
  via `sg_d3d11_device()`. The new work lands *inside* `sokol_app_tc.h`.
- **Per-window dt story?** None. Global `beginFrame()`/`present()`, single frame
  counter. The new `sokol_app_tc.h` already carries a per-window EMA frame timer
  (header ~line 206) — that becomes the dt source; this branch offers nothing here.
- **Known-broken / deferred:** the keycode table (line 436, "Phase 2 polish") — the
  only functional gap. No text input (`WM_CHAR`). No TODO/FIXME/hack strings
  otherwise; the code is otherwise complete and self-describing.
