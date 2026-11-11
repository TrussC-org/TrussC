# TrussC Modifications to Sokol Headers

This ledger records the fork features, their reasons, current code and planned
maintenance. The owner decisions relayed on 2026-11-11 supersede the audit's
rollback proposals: R1, R2, R4, synchronized application-frame drawing, iOS BC,
iOS orientation/immersive, Android density and 32 touches stay. This ledger-only
slice changes no behavior. A planned move, revert or deletion below describes
future work; that code is still present and must survive an upstream update
until its implementation slice lands.

## Provenance and classification

**Upstream base:** https://github.com/floooh/sokol commit `e7a48ed6` (2026-11-03).

This machine-readable base record applies to the patched copies `sokol_gfx.h`,
`sokol_glue.h` and `util/sokol_gl_tc.h` (upstream `util/sokol_gl.h`).
`sokol_app_tc.h` has original `sokol_app.h` lineage, graduated into the standalone
TrussC fork in P0–P6 (`6d73d9a2`, `6686f275`, `30da93d1`, `7a1347e2`,
`2bfaf1a5`, `f3ac1c8f`, `0295b8e3`); `e7a48ed6` is its **compatibility sync**
revision, not a whole-implementation rebase. Preserve the zlib license and
`tettou771` attribution. Absorbed upstream documentation is historical where
it disagrees with the current code/feature contracts below; the fork does not
implement every newer upstream enum, diagnostic, swapchain option or backend.

Shader compiler: `11d0cf678105d614d675e6d9bd2aaf3eeff12f8c` (2026-08-29T14:15:01Z), the
owner-selected master HEAD used by CI. The host hashes are recorded in
`core/cmake/trussc_shaders.cmake`; update the commit and all five hashes together
and check the generated shaders against these vendored headers. This compiler
preserves the `metal_sim` and `wgsl` outputs required by TrussC.

Search for `tettou771`, `Modified by` or `[TrussC` to locate existing blocks.
Feature locations below use symbols or comment markers, never drifting line
numbers. Some entire native sections were rewritten; marker counts alone do
not establish complete preservation. Event step 3a will normalize surviving
public fork APIs/types to `sapp_tc_*` and private helpers to `_sapp_tc_*`, exempting
upstream names and local guard types. Every member added to an upstream struct
gets its own `[TrussC]`; wholly TrussC-owned structs get one block marker.
Existing attribution must remain. This slice does not perform those renames.
`Bound` annotations (`64b7ada0`) belong to the affected input, allocator, cache
and device-buffer features below; retain representation/security/device limits
with their code, without adding an allocator or recovery framework.

- **KEEP essential:** multiwindow (案2), RGB10A2, ordinary target/Metal multi-flush
  lifetime, device loss and required input/common-path safety; upstream-equivalent
  fixes remain with their owning feature.
- **KEEP decided by owner:** deliberate product behavior, including R1/R2/R4.
- **MOVE to TrussC (planned):** preserve behavior at the framework boundary.
- **REVERT (planned):** use the specified native/upstream behavior in a later slice.
- **DELETE unused (planned):** remove the listed surface and all reachable stubs,
  without aliases. Conditional probes are labeled explicitly; they are not
  permission to remove safety before verification.

The planned slice names below follow the owner decision's execution order:
ledger → independent singleton deduplication → event 1 (native context) → event 2
(Runtime injection/MCP) → event 3a (names/markers) → event 4–5 (#1404 A/B),
then R3 X11 one-day portability probe, R5 unused API retirement, R6 one-day Mac
quit probe, and separate R7 platform moves/reverts. Shared singleton API wrappers
follow the settled event boundary. R1/R2/R4 rollback slices and iOS BC removal
are canceled. V3D has only the conditional future removal stated below.
Each implementation slice must fit 1–2 days; stop if it needs a new engine.

## Files and platform scope

| File | Ownership |
|---|---|
| `sokol_app_tc.h` | Permanent standalone fork replacing `sokol_app.h` (graduated 2026-07); selectively cherry-pick upstream improvements. |
| `sokol_gfx.h` | Patched upstream GL/GLES, Metal, WebGPU rendering. |
| `sokol_glue.h` | One local RGB10A2 translation patch. |
| `sokol_log.h` | Unmodified upstream. |
| `util/sokol_gl_tc.h` | Renamed fork of `util/sokol_gl.h`; no other sokol header depends on it; local public API uses `sgl_tc_`. |
| `addons/tcxImGui/src/sokol_imgui.h` | e7a48ed6/#713 Q3/Q4 with `[TrussC]` `simgui_tc_*` multi-context patches; update alongside core, following its [ledger](../../../addons/tcxImGui/src/imgui/TRUSSC_MODIFICATIONS.md). |

Core/util layout matches upstream. Audio uses miniaudio instead of
`sokol_audio.h` (rationale: `tc/sound/tcAudio_impl.cpp`). App's supported `sapp_*` API
and multiwindow implementation are self-contained, retaining zlib licensing:
macOS NSWindow/CAMetalLayer/CADisplayLink; Win32 HWND/D3D11/DXGI waitable;
X11 GLX and Pi GLES3/EGL. Single-window backends: Web rAF/WebGPU/WebGL2,
iOS UIWindowScene/Metal, Android NativeActivity/EGL/Choreographer.
Contracts: `docs/dev/sokol_app_tc-design.md`, `docs/dev/sapp-*-impl-spec.md`.

## sokol_app_tc.h (replaces sokol_app.h)

### Native window identity, creation and per-window input (#239, #1006)

**Classification:** KEEP essential.

**Code locations:** `SOKOL_APP_TC_INCLUDED`, `SOKOL_APP_INCLUDED`, `SOKOL_APP_TC_IMPL_INCLUDED`, `SOKOL_APP_TC_IMPL_DONE`; `sapp_window`, `sapp_window_desc`, `sapp_create_window`, `sapp_tc_window_read_metadata`, `sapp_tc_window_close_recorded`; `util/sokol_window_native.h` (`_sapp_tc_next_window_id`, `_sapp_tc_normalize_main_window`, `_sapp_tc_normalize_window`), `util/sokol_window_creation.h` (`_sapp_tc_creation_transaction`).

**Planned slice:** event 3a for surviving names/markers; no ownership rollback.

Fork/include/implementation guards and SOKOL_APP_INCLUDED compatibility keep
C++/Objective-C++ and third-party glue usable. Desktop IDs are never reused;
one native constructor/record per backend publishes ready/bind metadata only
after transactional creation/unwind. macOS owns window/layer/source records;
Win32 shares D3D11 with per-window flip swapchains; X11 GLX/EGL share a GL context
and per-window surfaces (#1019 `5a7d5c36`). Native key/button/lock/cursor state
belongs to its receiver; app-wide/custom cursor routing uses it. iOS/Android/Web
reject secondaries with reachable stubs. Upstream e7a48ed6 has no released
multiwindow equivalent (#437/#229).

### GLES3 window registry growth

**Classification:** REVERT (planned).

**Code locations:** `_sapp_tc_x11_reserve_slot`, `_sapp_tc.windows` in the GLES3 branch.

**Planned slice:** R7 GLES3 registry reversion.

GLES3 currently grows its registry; R7 restores a fixed cap like macOS, Win32
and GLCORE. This registry choice is separate from retained R2 render/context growth.

### Native source admission, pacing and host turns (#239, #997, #1012, #1263)

**Classification:** KEEP essential (ownership/admission); KEEP decided by owner (R3 cadence).

**Code locations:** `_sapp_tc_mac_select_source`, `_sapp_tc_mac_source_tick`, `_sapp_tc_mac_tick_body`, `_sapp_tc_mac_host_turn`, `applicationDidFinishLaunching:`; `_sapp_tc_win32_tick`, `_sapp_tc_win32_modal_tick`, `sapp_run`; `_sapp_tc_x11_update_pacer`, `_sapp_tc_x11_tick`, `_sapp_tc_x11_window_due`, `_sapp_tc_x11_run_loop`, `last_tick_presented`; singleton `_sapp_tc_call_init` / `_sapp_tc_call_event`; `sapp_skip_present`, `sapp_tc_window_skip_present`.

**Planned slice:** R3 X11 one-day portability probe after event 1–5; move processing to TrussC with only a trigger in sokol where useful; leave it if savings are small; no cadence rollback.

Native sources admit safe window work and host turns even without drawing.
R3 keeps all windows drawing the same application frame on the same tick;
“as little drift as possible” is the last resort when Mosaic cannot be used.
Visible pacer progress, no hidden swaps, responsive OS input, one-use waitable
credits and lazy drawable acquisition belong to this single admission feature.

| Backend/history | Contract and reason |
|---|---|
| macOS #239 a2/checkpoint 4 (`eff4fbe5`, `416d0ecd`, `47f88f8e`, `c2c1870d`, `8607cde2`) | Per-window CADisplayLink, fallback timer, per-screen refresh and late drawables; native visibility/source selection, callback containment and deferred visibility/key processing protect live records. No generalized queue or extra rare nested-loop guards. |
| macOS host-turn observer #239 | Seed observer nesting depth from `CFRunLoopCopyCurrentMode`, releasing the sampled mode: `applicationDidFinishLaunching:` can precede run-loop entry, so assuming depth one suppressed all outer-loop reload/process/MCP work. Installation within a running loop still starts at one; native tick/init and nested arbitration are unchanged. |
| macOS test entry | `_sapp_tc_mac_tick` stays until tests exercise real source admission; retarget/removal requires a separate native check. |
| Win32 P1 `30da93d1`, double-wait `cb28ad56`, #481 `068bda0a`, #239 `f81532b5`/`f7156155` | Shared-device flip swapchains use frame-latency waitable credits, high-resolution fallback and modal resize ticks. Consumed signals differ from held credits; skipped presents must neither spin nor starve. |
| Win32 input #1012 | Positive-interval windows enable shared input wake; clear delivery flags before draining messages, reset only receiving windows' skipped-present deadlines, then apply the existing credit admission. |
| X11 intervals #1263 | GLX/EGL followers share the context and native interval 0; store requested N (nonpositive means 1), admitting every Nth pacer presentation/fallback-refresh credit. Default/interval-1 cadence stays; partial counts survive fallback takeover, without a new thread/timer. |
| X11 skipped ticks #998/#1212 | Feed `_sapp_tc.refresh_period` only when `last_tick_presented` is true: `skip_present` gaps under nonblocking compositors otherwise teach a false 30 Hz display and slow interval-zero followers. |
| X11 input #1012 | Clear delivery flags and reset the affected `earliest_next` after GLX/EGL event delivery, admitting input in the same skipped-present wait turn. Shared due-check/EGL behavior stays. |
| Movable X11 pacer #997, Q31 D | Earliest-created ready/drawable/effectively visible window owns native vsync at N and runs first. Demote the surviving old drawable to 0, promote once, reset presentation history/deadline. Creation records the role without reapplying interval; surface renewal reapplies it. GLX EXT is per-drawable; MESA makes the intended drawable current and retains the multiwindow interval-0 timer fallback. |
| X11 hidden work | Never swap hidden windows. Draw-while-hidden ON retains draw/GPU submission, OFF retains updates; both use the periodic base-refresh timer. `is_main` only selects client init. Win32/macOS and covered-window detection are unchanged. |
| Host bridges #239 C4 (`b8e542af`, `6bb9ee00`, `df87610f`) | Desktop host turns and singleton init/event bridges keep Runtime progressing without drawing. Event 2 relocates injection/MCP with existing scheduling/dev-tool wake limits; no immediate injected-input deadline-cancel wake protocol. |
| Skip present, historical D3D11 flicker fix | Suppress event-driven presentation without dropping owner completion. Explicit checks: D3D11, X11 GLX/EGL, Android EGL. macOS consumes the one-shot at tick boundary; gfx owns Metal presentation. iOS flag is inert; Web stores it for parity but rAF auto-presents (non-Web WebGPU present branch excluded). No WGL/Vulkan app backend. |

Verification anchors: `windowTick/x11-pacing-test` (both backends, input,
throttled-main and frozen N=1/N=2 seams), `win32-pacing-test`, `egl-wait-test`,
`native-gates-test`, `native-admission.py`, `test_x11_movable_pacer.py`,
`test_host_turn_hooks.py`, `test_window_event_gates.py`. Later restructuring
requires device cadence checks (including gdm GLX/Pi EGL interval 2 at ~30/60 Hz).

### Native close, quit and async dialog preflight (#239, #266, #1117)

**Classification:** KEEP essential (close/teardown); REVERT (planned, conditional R6 preflight removal only).

**Code locations:** `_sapp_tc_mac_close`, `_sapp_tc_mac_closed`, `_sapp_tc_mac_process_deferred`, `_sapp_tc_application` (`terminate:`), `handleQuitAppleEvent:withReplyEvent:`, `applicationShouldTerminate:`, `applicationWillFinishLaunching:`; `_sapp_tc_win32_close`, `_sapp_tc_x11_close`; `tcExit.h` and Runtime session-ending bridge.

**Planned slice:** R6 one-day Mac quit preflight feasibility probe; remove only if safe, otherwise keep.

Close/quit ends targets before sg/sgl shutdown, preserves ordinary vetoes and
separates OS session ending from ordinary quit. #239 C2/C3 (`b55258f2`,
`dab378aa`, simplification `020692e4`) defer native close/terminate while records
are live, without a second backend session state machine/deferred-state engine.

| History | Contract and reason |
|---|---|
| macOS async preflight #1117, PR #1254 | `sapp_run` creates `_sapp_tc_application`; its `terminate:` calls `applicationShouldTerminate:` before AppKit can dismiss/refuse async confirm/save sheets. Menu Quit/Cmd+Q/SIGTERM use deferred sokol quit; ordinary Dock/Apple-event quit calls `[NSApp terminate:nil]`. |
| Apple-event installation | `applicationWillFinishLaunching:` replaces `kCoreEventClass` / `kAEQuitApplication` after default installation, only for `_sapp_tc_application`. Runtime veto precedes AppKit; accepted-quit cancellation/shutdown is idempotent, so AppKit's second query repeats neither veto nor cancellation. |
| Apple-event replies/session end | Synchronous handling keeps the event as `currentAppleEvent` for its quit reason and existing query/commit policy. On veto/deferral set requested reply `keyErrorNumber` to `userCanceledErr` (also tells loginwindow); leave `typeNull` untouched. Last-window-close policy stays. |
| Pre-created application limitation | `sharedApplication` leaves an existing plain NSApplication unchanged, losing preflight. TrussC creates none earlier; apps should not either. |
| Accepted rare interleaving | Ignore nested `terminate:` including sheet animations; reset the guard on veto. Session end inside an exit-handler loop is ignored and lost if that ordinary quit is vetoed; documented, not guarded. |
| OS exit #266/#239 C3 | Runtime owns query/commit and reverse-attachment ending. Win32 user windows acknowledge broadcasts without exiting; Runtime's hidden top-level HWND (`tcPlatform_win.cpp`) receives them. macOS `applicationShouldTerminate:` forwards session queries/commits, `NSWorkspaceWillPowerOff` commits directly, busy ordinary quit defers. Accepted ordinary quit shuts Runtime down before `NSTerminateNow`, protecting target lifetime. |
| Busy/idle session commit | Busy main-stack commit calls no App exit/arms no watchdog: Win32 returns to WM_ENDSESSION, macOS `_exit(0)` precedes NSTerminateNow. Idle runs synchronous normal ending/termination under one OS-only 3-second default deadline; no session freeze/second notification runner. Idle closes Logger and flushes stdout/stderr; busy does not flush. See [EXIT_PROTOCOL.md](../../../docs/EXIT_PROTOCOL.md) for signals, Logger markers and MCP drain. |
| Failed startup #239 | Cleanup supplies NSApplicationWillTerminateNotification with NSApp as object, matching AppKit's nonnull contract and avoiding clang 21 `-Wnonnull`; normal order is unchanged. |

R6 probes only special macOS preflight removal: normal quit, open async sheets
and logout must still tear down safely, otherwise keep it. Common close,
callback lifetime, dialog veto and target teardown safety remain.
Anchors: `test_mac_dialog_quit.py`, `test_window_lifecycle_regressions.py`,
`sessionEndBusy/native-test.py`, `windowTick/native-gates-test`; R6 needs AppKit.

### Upstream environment/swapchain compatibility (#713, Q5/Q6/Q7)

**Classification:** KEEP essential.

**Code locations:** `sapp_acquire_swapchain`, `sapp_get_environment`, `sapp_swapchain.invalid`, `SAPP_PIXELFORMAT_RGBA16F`, `SAPP_PIXELFORMAT_SBGR8A8`; `_sapp_tc_d3d11_create_render_targets` depth texture creation.

**Planned slice:** singleton API deduplication after event 1–2; no new backend/options.

Compatibility sync replaces `sapp_get_swapchain` with `sapp_acquire_swapchain`
(no alias), propagates unavailable drawables via `sapp_swapchain.invalid`, adds
RGBA16F and renames SBGRA8→SBGR8A8 for glue. Main/secondary D3D11 depth uses
`D32_FLOAT_S8X24_UINT` to match gfx; iOS already links CoreGraphics.
Supported getters/reachable stubs stay; newer FORCE_U32 sentinels, Android
native-window getter, options/diagnostics and Vulkan/WGL backends are not imported.
Local device-loss/drop-type/touch and obsolete fixed-buffer diagnostics are
history, not enum-import/renumbering instructions. Color/multiwindow/present
contracts belong to their entries.

### Creation sizes in pixelPerfect mode (#1018/#1031)

**Classification:** KEEP essential.

**Code locations:** `sapp_desc._tc_pixel_width`, `_tc_pixel_height`; `_sapp_tc_mac_create_window_ex`, `_sapp_tc_win32_create_window`, `_sapp_tc_x11_initialize_dimensions`; `util/sokol_window_size.h` (`_sapp_tc_window_points`, `_sapp_tc_window_pixels_representable`).

**Planned slice:** none; retain the current behavior.

The internal `_tc_pixel_width` / `_tc_pixel_height` descriptor fields preserve
positive framebuffer requests until the native target's render DPI is known.
Zero keeps the existing logical/default path. Win32 folds conversion into its
existing single DPI resize; X11 GLX and GLES3/EGL convert before XCreateWindow;
macOS uses one setContentSize before showing the window. The pure C/C++ helper
in `util/sokol_window_size.h` is also used by TrussC's resize dispatcher, so
macOS main and secondary retain the same double point sizes. Normal and
hot-reload launchers share descriptor construction; creation keeps the existing
handling of invalid sizes. Native pixel conversion checks the same logical-size
representability limit as the resize dispatcher before applying the request.

### Requested/native visibility, fullscreen and resize (#1262/#1273)

**Classification:** KEEP essential.

**Code locations:** `sapp_tc_window_set_visible`, `sapp_tc_window_visible`, `sapp_tc_window_map_complete`, `sapp_tc_window_fullscreen_phase`; `_sapp_tc_x11_sync_visibility`, `_sapp_tc_x11_process_event`, `_sapp_tc_x11_set_fullscreen`, `_sapp_tc_x11_observe_fullscreen`; `util/sokol_window_native.h`.

**Planned slice:** none for ordinary visibility/resize; parent-specific handling leaves with R7 parenting.

Requested hiding differs from OS occlusion; constructors, decoration/fullscreen,
Runtime accounting and native presentation respect it. Two internal TC redraw
notices preserve EVENT_DRIVEN map/resize drawing. Parent policy is separate (R7).

| Stage | Behavior and reason |
|---|---|
| Hidden startup | `sapp_query_desc()` added on macOS (already on Win32/X11) exposes `_tc_start_hidden`. Main/secondary constructors skip show; macOS selects fallback ticks and defers fullscreen until shown. `applicationShouldTerminateAfterLastWindowClosed:` returns NO; Runtime owns quit-on-close. |
| Native visibility | `orderOut:`/`orderFront:`, `ShowWindow(SW_HIDE/SW_SHOWNA)`, `XWithdrawWindow`/`XMapWindow`; metadata/drawable admission honors explicit hide even when OS signals are disabled. Hidden children attach only while shown; X11 parent hint precedes return. Unsupported seams do nothing; public API logs Notice. |
| Follow-up 1 | Skip launch activation/synthetic focus for hidden macOS; decoration and Windows fullscreen entry/restore retain hiding (clear WS_VISIBLE); bringToFront ignores hidden windows. Runtime's post-draw checks and OnceGate Notices were superseded below. |
| Follow-ups 2–4 | Notices on every unsupported call; Win32/X11 recheck at native swap, Metal cancels immediately before present authorization/commit, covering afterFrame. Runtime accounts after present, replacing post-draw checks. macOS trust opt-out preserves OS-only suspension reset; explicit hides retain recovery notification. |
| Follow-up 7 (superseded map algorithm) | Withdraw late requested-hidden MapNotify; visible mapping requests one coalesced Runtime redraw without app events, preserving EVENT_DRIVEN content across asynchronous WM presentation. |
| Follow-up 8 | Explicit-show flag limits fullscreen replay to that show; ordinary remaps retain user fullscreen and request redraw. Withdrawal can lose Always on Top/workspace state; only requested fullscreen is restored, as documented in the public reference. |
| Follow-up 9 (current) | One operation state serializes map/withdraw; matching MapNotify/UnmapNotify completes it and reconciles latest intent, including initial visible creation. Native loop turns check a 500 ms acknowledgment bound, sample server map state and retry latest intent even without ticks. Ordinary WM minimization never remaps. Replaces late-map special case; explicit-show fullscreen replay/redraw stay. |
| Follow-up 10 (current) | MapNotify also reconciles requested hiding after map timeout, withdrawing late maps without changing unsolicited UnmapNotify handling for requested-visible windows. |
| #1273 map completion | `sapp_tc_window_map_complete()` requires X11 `native_visible`, no pending operation and no requested hide; TrussC retains hidden frame-position snapshot until then. Other platforms return existing requested visibility. |

### Z-order parent relationships

**Classification:** REVERT (planned).

**Code locations:** `sapp_window_desc.parent`; `_sapp_tc_mac_create_window_ex` / parent-child detach; `_sapp_tc_win32_create_native_window` / owner detach; `_sapp_tc_x11_create_native_window` (`WM_TRANSIENT_FOR`).

**Planned slice:** R7 parenting reversion.

The parent addition (`b51133b0`) keeps a window above its parent without
sharing geometry ownership. AppKit detaches children before ordering out or
closing a parent; Win32 clears owners before DestroyWindow can destroy children;
X11 sets the transient hint before hidden creation returns. Hidden children
attach only when shown. The owner chose independent top-level windows: remove
descriptor, native hints and corresponding teardown/visibility handling together,
while preserving ordinary multiwindow lifetimes and hidden startup.

### Native event context and retained TC notifications (#239, #258, #1262)

**Classification:** MOVE to TrussC (planned) for six fields; KEEP essential for device loss and redraw kinds.

**Code locations:** `sapp_event` (`window_id`, `dpi_scale`, `pointer_locked`, `reliable_button_bits`, `source`, `button_snapshot`, `device_lost_reason`); `util/sokol_window_event.h`; `SAPP_EVENTTYPE_TC_DEVICE_LOST`, `SAPP_EVENTTYPE_TC_REDRAW_REQUESTED`, `SAPP_EVENTTYPE_TC_RESIZE_REDRAW_REQUESTED`.

**Planned slice:** event 1 moves target ID/DPI/lock/reliability outside raw sapp_event; event 2 moves source/snapshot; event 3a marks surviving members.

The six extra fields currently duplicate binding context or TrussC injection
state. Move all six to TrussC, snapshotting target context before raw conversion;
keep `device_lost_reason` and the three TC kinds. Do not pursue byte-identical
upstream events by removing required loss/resize/map notifications. The source
and snapshot relocation is described by the injection feature below, not a
second native event implementation. Later #1404 A/B restores the approved
Window-owned injection surface and typed text/drop/ImGui coverage.

### Injected input provenance and private MCP drag snapshot

**Classification:** MOVE to TrussC (planned).

**Code locations:** `sapp_event_source`, `sapp_window_inject_event`, `sapp_event.source`, `sapp_event.button_snapshot`; `util/sokol_window_event.h` and Runtime converters.

**Planned slice:** event 2 Runtime injection/MCP; then #1404 A/B (event 4–5).

Keep `injected`/`sourceTag` only on TrussC typed arguments. Delete sokol's
injection function and source enum in step 2; preserve state and raw-before-typed
notification so simgui receives the same DOWN/MOVE/UP. Preserve the private MCP
`tc_mouse_move(button)` drag-only held-button snapshot during relocation; full
gestures still use press/move/release. Current scheduling stays, including the
dev-tool wake limitation recorded with source admission. New public API usage
goes to the owner before its later implementation.

### Touch capacity

**Classification:** KEEP decided by owner (32 touches).

**Code locations:** `SAPP_MAX_TOUCHPOINTS`, `sapp_event.touches` and native touch converters.

**Planned slice:** none; retain the current behavior.

Keep 32 touch points (`675b946a`) through native event cleanup and later work.
The audit's optional eight-touch rollback is canceled. Overflow/changed-finger
validation remains part of native input conversion, not a new arbitrary cap.

### Logger routing and startup/exit diagnostics (#265, #266)

**Classification:** KEEP essential.

**Code locations:** `_sapp_tc_log`, per-platform `_SAPP_*` log macros, `sapp_desc.logger`, `sapp_run` error/unwind paths and Android `ANativeActivity_onCreate`.

**Planned slice:** none authorized; retain diagnostic routing while singleton helpers are shared.

Fork diagnostics use the installed sokol logger so startup errors and their
exit results reach TrussC's log file (`6ff926e7`, `7a981f98`, `c9b009a1`,
`212fcde2`, startup `e9e4ef71`). Without a callback they retain stderr fallback;
Android uses logcat before/without the descriptor because stderr is unavailable.
The audit's logger-policy simplification is not an approved behavior rollback.
Prefer shared upstream-shaped helpers only when those failure routes remain.

### RGB10A2 surfaces and glue translation

**Classification:** KEEP essential.

**Code locations:** `SAPP_PIXELFORMAT_RGB10A2`, `sapp_color_format`, Metal layer/MSAA texture creation, Win32 swapchain creation/resize; `sokol_glue.h` (`_sglue_to_sgpixelformat` RGB10A2 case, `Modified by tettou771`).

**Planned slice:** none; retain the current behavior.

RGB10A2 reduces 10-bit-display banding at BGRA8's 32-bit bandwidth. Metal
(macOS/iOS layer and MSAA) uses `MTLPixelFormatRGB10A2Unorm`; D3D11 swapchain,
MSAA and resize use `DXGI_FORMAT_R10G10B10A2_UNORM`. `sapp_color_format()`
returns the added `sapp_pixel_format` value `SAPP_PIXELFORMAT_RGB10A2`; WebGL stays unchanged. Glue's sole local
patch maps it to `SG_PIXELFORMAT_RGB10A2`. Historical tettou771/core-import
`f7810df0`, graduation and #713 `3961fa0f`; output precision and R4 vertex
precision address different problems.

### Growable drop/clipboard storage and native transfer ownership (#239, #1023, #866, #878)

**Classification:** KEEP decided by owner (R1 growth); KEEP essential (protocol/input bounds and async ownership).

**Code locations:** `_sapp_tc_clipboard_text`, `_sapp_tc_dropped_paths`, `_sapp_tc_x11_serve_selection_request`, `_sapp_tc_x11_receive_incr`, `_sapp_tc_x11_parse_dropped_files_list`; `_sapp_tc_js_add_clipboard_listener`, `_sapp_tc_emsc_free`, `sapp_html5_get_dropped_file_size64`, `_SAPP_TC_MAX_DROP_CONTENT_SIZE`, `_SAPP_TC_MAX_DROP_READERS`; `_sapp_tc_mac_create_window_ex` file-drop registration.

**Planned slice:** none; retain the current behavior.

R1 replaces `sapp_desc::max_dropped_files`, `max_dropped_file_path_length` and
`clipboard_size` with `std::string`/`vector` growth, without replacement caps,
settings API, preallocated slots or OOM recovery/retry machinery.

| Path/history | Contract and reason |
|---|---|
| Desktop/Web drops | Retain all paths; Win32 queries UTF-16 length (`DragQueryFileW`) then UTF-8 conversion size; X11 chunks URI properties and grows decoded paths, including INCR. |
| Clipboard | Actual-content growth on desktop/Web paste/copy and internal iOS/Android clipboard. Win32 queries conversion lengths; X11 supports full properties/INCR both ways. Web uses heap UTF-8 instead of fixed WASM stack. |
| Boundary follow-up 1 | Normal C++ allocation failure applies. Win32 window-procedure boundary logs `bad_alloc` drop/event/clipboard Errors; direct clipboard calls propagate it. Web catches/returns allocation status, then JS raises OOM Error after C++ unwinds and frees text in `finally`. Win32 `GlobalAlloc` failure logs Error/returns instead of throwing from C API. |
| Heap export follow-up 2 | Web frees paste/drop text via exported `_sapp_tc_emsc_free`, avoiding `_free` abort when free is not exported (no ASYNCIFY/FETCH, e.g. `webDropSize`). |
| X11 TARGETS #1023 | Advertise `{TARGETS, UTF8_STRING}` per ICCCM; not TIMESTAMP (would require acquisition-time tracking). Shared GLX/EGL code. |
| macOS #239 D-min | Register file URLs on every content view, including secondaries; existing handler routes to its own receiver, with unchanged rejection behavior. |
| Web #866/#878 security bounds | Check full 64-bit file size before allocation: four readers × 256 MiB = at most 1 GiB pending content. Cancel readers and owned callbacks/heap bridges on teardown; validate size/type/UTF conversion. Separate from uncapped paths/clipboard. |

Anchor: `test_drop_clipboard_unbounded.py` (production X11/Xvfb and Win32/Web seams).

### Pre-run DPI queries (#841)

**Classification:** REVERT (planned).

**Code locations:** `sapp_dpi_scale` in all six platform sections (macOS, Win32, shared X11 GLX/EGL, iOS, Android, Web).

**Planned slice:** R7 DPI reversion at all six sites, together with TrussC getDpiScale/windowDimensions.

Current code returns native screen DPI on macOS and 1.0 fallback on Win32,
shared X11, iOS, Android and Web before initialization. #841 added the singleton
fallback (`58290fff`) because headless DPI 0 made every font size share a cache
key. The old note described X11/EGL as a separate implementation; GLX/EGL now
share one site. The owner chose the #841 “one rule” at the TrussC source:
headless (no window) DPI = 1.0; a windowed app before its window exists returns 0.
Remove the sokol pre-run overrides together, without losing the headless font fix.

### macOS borderless key/main windows

**Classification:** KEEP essential.

**Code locations:** `_sapp_tc_window` (`canBecomeKeyWindow`, `canBecomeMainWindow`).

**Planned slice:** none; retain the current behavior.

A borderless NSWindow answers NO to `canBecomeKeyWindow`, so it never got
keyboard input (ESC in a clicked borderless window closed another window).
Every window is now created as `_sapp_tc_window`, an NSWindow subclass that
returns YES from `canBecomeKeyWindow` and `canBecomeMainWindow`. Titled windows
already returned YES, so decorated windows behave as before.

### macOS first activating click

**Classification:** KEEP essential.

**Code locations:** `_sapp_tc_view` (`acceptsFirstMouse:`).

**Planned slice:** none; retain the current behavior.

`_sapp_tc_view` answers YES to `acceptsFirstMouse:`, so the click that activates
a window also reaches the app, as on Windows and Linux (upstream sokol_app and
GLFW do the same). This covers clicks between the app's own windows (a palette
click picks a color at once) and the click that brings the app back from
another app.

### macOS readable secondary drawables (#879)

**Classification:** KEEP essential.

**Code locations:** `_sapp_tc_mac_create_window_ex` (`framebufferOnly = NO`, `[TrussC] captureWindow()` marker).

**Planned slice:** none; retain the current behavior.

- Secondary-window `CAMetalLayer`s now set `framebufferOnly = NO`, like the
  main window and iOS. Window capture blits from the drawable texture, which
  Metal does not allow for a framebuffer-only texture (validation aborts with
  "sourceTexture must not be a framebufferOnly texture").

### macOS file-open requests (#1061)

**Classification:** MOVE to TrussC (planned).

**Code locations:** `application:openURLs:`, `application:openFiles:`; `tcFilesOpened.h` include (`[TrussC #1061]`).

**Planned slice:** R7 macOS file-open move, leaving a minimal delegate-to-TrussC callback.

Delegate URL/legacy file callbacks forward file URLs to Runtime's process-level
filesOpened delivery: queue launch requests through launch-App setup, then
deliver to all listeners. OS/memory/container path bounds match file drops;
no shell or content reads. R7 moves filtering/processing and the framework-header
dependency behind a minimal callback, preserving delivery (audit rollback canceled).

### Win32 executable window icons (#1387)

**Classification:** MOVE to TrussC (planned).

**Code locations:** `_sapp_tc_win32_create_native_window`, `class_icon`, `class_icon_small`, `[TrussC] Match the exe resource` and owned-icon cleanup markers.

**Planned slice:** R7 Win32 icon move: WM_SETICON in the TrussC bind step, identical behavior.

The shared main/secondary window class uses `WNDCLASSEXW` and loads `IDI_ICON1`,
matching the icon resource generated by core CMake. Big and small icons are
loaded separately with `LoadImageW` at their system metrics, without the shared
resource-name cache. Owned images are destroyed after window/class teardown.
Each falls back to `IDI_WINLOGO` when the executable has no icon resource.

### Win32 console UTF-8 input/output (#754)

**Classification:** MOVE to TrussC (planned).

**Code locations:** `_sapp_tc_win32_init_console`, `_sapp_tc_win32_restore_console`, `console_utf8`; TrussC.h `ConsoleCPCtrlGuard`.

**Planned slice:** R7 Win32 console move, identical behavior including independent restoration.

Save `orig_output_codepage`/`orig_input_codepage`, set output then input to UTF-8,
and restore each independently only if its set succeeded; descriptor comments
carry this contract. Host-only `TrussC.h` `ConsoleCPCtrlGuard` also saves both
for Ctrl+C/Ctrl+Break/console close and one `atexit` hook. Its destructor restores
before disarming fallback: redundant after normal sokol restore, essential when
an escaping `sapp_run()` exception is caught. `abort()`, `TerminateProcess` and
external kills cannot restore. Input remains `std::getline(std::cin)`.

### Win32 keep-screen-on power commands (#1041)

**Classification:** MOVE to TrussC (planned).

**Code locations:** `sapp_tc_win32_set_keep_screen_on`, `_sapp_tc_win32_keep_screen_on`, WM_SYSCOMMAND (`[TrussC #1041]`).

**Planned slice:** R7 Win32 power move: TrussC already calls SetThreadExecutionState; drop the sokol flag, retain upstream fullscreen screensaver block.

The Windows-only `sapp_tc_win32_set_keep_screen_on` seam stores one process-wide
flag, updated on the host main thread alongside `SetThreadExecutionState` and
cleared by the existing disable/normal-cleanup path. The `WM_SYSCOMMAND` handler
refuses `SC_SCREENSAVE` and `SC_MONITORPOWER` when that flag is set or any app
window is fullscreen. This applies to every TrussC HWND. Windows does not ask
TrussC when another app is in front, so screen-saver suppression has that limit.
The existing fullscreen policy is unchanged.

### D3D11 borrowed views (#910)

**Classification:** KEEP essential.

**Code locations:** `sapp_window_d3d11_render_view`, `sapp_window_d3d11_resolve_view`, `sapp_window_d3d11_depth_stencil_view`.

**Planned slice:** none; retain the current behavior.

The three accessors return borrowed existing views outside native ticks too;
valid until that window resizes/dies, without acquire/retain. This enables lazy
secondary capture during foreign-window execution. Metal secondary drawable
acquisition still requires its native tick.

### D3D11 device loss (#258)

**Classification:** KEEP essential.

**Code locations:** `util/sokol_d3d11_device_loss.h`, `SAPP_EVENTTYPE_TC_DEVICE_LOST`, `sapp_event.device_lost_reason`, `_sapp_tc_win32_tick` and Present/ResizeBuffers classification.

**Planned slice:** none; retain the current behavior.

Main/secondary/occlusion-test Present and ResizeBuffers classify only
DEVICE_REMOVED/DEVICE_RESET, not other errors/statuses. First shared-device loss
latches before callbacks, logs HRESULT/GetDeviceRemovedReason, and delivers
SAPP_EVENTTYPE_TC_DEVICE_LOST plus reason to the main callback once.
`events().deviceLost` cancellation continues callbacks; default logs Error/exits 1.
After loss use timer pacing, no dead waitables, present, resize or resource recreation.
First tick still sizes/resizes before init. Pre-init loss logs/latches its reason,
then dispatches immediately after init and before first frame (default exit skips
it), clearing pending state before dispatch to prevent reentrant duplication.

### X11 shared/idle GL context (#239, #821, #910)

**Classification:** KEEP essential.

**Code locations:** `_sapp_tc_x11_make_current`, `sapp_tc_gl_activate_idle`, `sapp_tc_gl_ensure_context`, `sapp_tc_gl_save`, `sapp_tc_gl_restore`, `sapp_tc_gl_make_current`.

**Planned slice:** none; retain the current behavior.

`sapp_tc_gl_activate_idle()` reuses the drawable created with the hidden X11
window; creating a second EGL surface for that window fails with EGL_BAD_ALLOC.
`_sapp_tc_x11_make_current(nullptr)` selects this idle drawable. Native close
selects the closing window before callbacks. Host-turn and cleanup entry, plus
shared queue and maintenance work, ensure a context is current before GL work,
selecting idle only when EGL/GLX reports no current context. This avoids idle
surface switches every frame. Window teardown and runtime shutdown still force
idle activation at disposal boundaries. Other backends keep the
internal ensure-context seam as a no-op, like idle activation.

### iOS orientation, immersive view and drawable dimensions

**Classification:** KEEP decided by owner (orientation/immersive); KEEP essential (actual dimensions).

**Code locations:** `_sapp_tc_ios_view_ctrl`, `_sapp_ios_immersive_mode`, `sapp_ios_set_supported_orientations`, `_sapp_tc_ios_update_dimensions`, `_sapp_tc_ios_mtl_update_framebuffer_dimensions`.

**Planned slice:** none; retain the current behavior.

| Feature | Contract and reason |
|---|---|
| Runtime orientation | `_sapp_tc_ios_view_ctrl` replaces UIViewController; `supportedInterfaceOrientations` reads iOS state's `supported_orientations` (default `UIInterfaceOrientationMaskAll`). `sapp_ios_set_supported_orientations()` updates it, calling `setNeedsUpdateOfSupportedInterfaceOrientations` on iOS 16+. |
| Immersive | Non-static `_sapp_ios_immersive_mode` remains accessible to `tcPlatform_ios.mm`; view-controller `prefersStatusBarHidden`/`prefersHomeIndicatorAutoHidden` hide status bar/home indicator. |
| Dimensions | `_sapp_tc_ios_update_dimensions()` uses `_sapp_tc.ios.view.bounds` (screen fallback below 1.0), passing `view_rect` instead of `screen_rect`; screen bounds can lag layout. `_sapp_tc_ios_mtl_update_framebuffer_dimensions()` reads `layer.drawableSize` into `framebuffer_width`/`framebuffer_height` so `sapp_width()`/`sapp_height()` match the pass and scissors cannot exceed it. |

Retain single-window UIWindowScene/CADisplayLink, P4 `f3ac1c8f` Metal swapchain
factoring and 10-bit/capture adjustments; orientation/immersive rollback canceled.

### iOS accepted process exit (#1304)

**Classification:** REVERT (planned).

**Code locations:** `_sapp_tc_ios_frame` (`[TrussC #1304] Finish the accepted quit`).

**Planned slice:** R7 iOS exitApp move/reversion.

[TrussC #1304] After a frame returns with an accepted quit, run the existing
cleanup callback before releasing iOS native state and shared backend state,
matching `applicationWillTerminate`. Then call `exit` with the requested process
code, so programmatic quitting completes instead of leaving the process alive.

This is the current patch, not the intended future iOS policy. Remove it in
R7; TrussC `exitApp` on iOS must log “iOS apps cannot quit themselves” on every
call and do nothing else. Normal native termination cleanup remains.

### Android BACK consumption

**Classification:** KEEP decided by owner (consumption); MOVE to TrussC (planned) for policy notification.

**Code locations:** `_sapp_tc_android_key_event` (`[TrussC tettou771]` BACK marker).

**Planned slice:** R7 Android BACK hook and Android-only Runtime event, public usage to owner first.

Consume `AKEYCODE_BACK` (`return true`) instead of `_sapp_tc_android_shutdown()`:
upstream destroys EGL and calls `cleanup_cb` immediately, but screen pinning
(lock-task) blocks `ANativeActivity_finish()`, leaving a live frozen process.
R7 adds a consuming TrussC hook and Android-only Runtime event (e.g. `backPressed`);
no listener keeps current behavior, listeners may call `exitApp()`. New API
needs en/ja/ko reference cards and owner usage review first. The old raw
QUIT_REQUESTED proposal is superseded.

### Android display density

**Classification:** KEEP decided by owner (density/160).

**Code locations:** `<android/configuration.h>`, `_sapp_tc_android_update_dimensions` (`[TrussC] Use actual display density`).

**Planned slice:** none; retain the current behavior.

Use `AConfiguration_getDensity()` (`<android/configuration.h>`) / 160 mdpi
for `sapp_dpi_scale()`, matching macOS/Windows; fall back to framebuffer/window
ratio if unavailable. The audit's DPI-ratio rollback is canceled. This post-init
rule is separate from pre-run DPI. Retain NativeActivity/Choreographer poll
fallback and app-thread lifecycle (P5, logging #265 `f615e9f2`).

### Web canvas keyboard focus

**Classification:** MOVE to TrussC (planned).

**Code locations:** `_sapp_tc_emsc_register_eventhandlers`, `_sapp_tc_emsc_unregister_eventhandlers` (`Modified by tettou771` keyboard markers).

**Planned slice:** R7 Web canvas key filtering move, identical behavior.

Register/unregister keydown/up/press on `_sapp_tc.html5_canvas_selector` instead
of `EMSCRIPTEN_EVENT_TARGET_WINDOW`, letting neighboring page controls (e.g.
TrussSketch's Monaco editor) handle their keys independently. Canvas needs
`tabindex` for focus; R7 moves filtering to the TrussC/Web host.

### Web tab resume rebase (#996)

**Classification:** MOVE to TrussC (planned).

**Code locations:** `_sapp_tc_emsc_visibility_cb`, visibilitychange registration/unregistration, `SAPP_EVENTTYPE_SUSPENDED` / `SAPP_EVENTTYPE_RESUMED` delivery.

**Planned slice:** R7 Web tab-resume timing move, identical behavior.

Visibility changes arrive while rAF is paused and rebase the shared scheduler
before the next tick (`e312f473`). Move that processing to TrussC without losing
resume behavior or inventing a browser event framework. WebGPU async device and
surface initialization, WebGL context setup and inherited lifecycle remain with
the upstream-shaped backend; skip-present belongs to source admission.

### Singleton support/API duplication

**Classification:** KEEP essential.

**Code locations:** copies of `_sapp_tc_timing_update` / other timing helpers, `_sapp_tc_desc_defaults`, `_sapp_tc_init_state`, `_sapp_tc_call_init`, `_sapp_tc_call_event`, `sapp_get_environment`, `sapp_acquire_swapchain` in iOS/Android/Web; unsupported-platform API stubs.

**Planned slice:** independent singleton timing/default/allocation deduplication first; shared API/bridge wrappers after event 1–2.

P3/P4/P5 copied the singleton support and public layer into three mutually
exclusive native sections. Restore one upstream-shaped singleton layer while
keeping local native adapters, required state and compatibility semantics.
The audit's whole-header census found up to 1,608 normalized redundant lines;
this is a ceiling, not a promised deletion. Prune impossible branches/private
helpers only with reachability proof; exported JavaScript bridges are live.
Device gates belong to those later implementation slices: iOS, Android,
WebGL2/WebGPU, plus desktop compile/link isolation. This ledger changes no layer.

### Unused window geometry/sample-count wrappers

**Classification:** DELETE unused (planned).

**Code locations:** `sapp_window_width`, `sapp_window_height`, `sapp_window_sample_count` declarations, desktop implementations and iOS/Android/Web/unsupported stubs.

**Planned slice:** R5 wrapper retirement; coordinate event 3a to avoid renaming retired symbols.

The owner approved deleting exactly these three unused wrappers without
aliases. Keep framebuffer/DPI/native handle and other live metadata accessors;
remove each retired declaration and every reachable definition together.

## sokol_gfx.h

Patched upstream e7a48ed6 copy; use the three-way process below. Its example
include spells `sokol_app_tc.h` (graduation P6 `7a1347e2`), covered by the fork
identity entry; this integration comment introduces no rendering behavior.

### Whole-attachment MSAA resolve and scissor (#861/#870)

**Classification:** KEEP essential.

**Code locations:** `_sg_gl_begin_resolve`, `_sg_gl_end_render_pass`, `_sg_gl_reset_state_cache`.

**Planned slice:** none; retain the current behavior.

`glBlitFramebuffer` obeys draw scissor, so resolving a clipped draw must disable
`GL_SCISSOR_TEST` for all full-extent color resolves, then restore enabled before
GLES invalidation, leaving the rectangle intact. No-resolve passes leave enable
state alone. `_sg_gl_cache_t` does not cache enable; `_sg_gl_reset_state_cache`
always enables it. V3D flush still precedes the first resolve. Readbacks use
unclipped `glReadPixels`; Fbo version copies/mips use full destination scissors.
Upstream `37e21167` absorbed the #861/#870 fix at e7a48ed6 (#713); the shared
helper still serves V3D and the reserved destination hook (R5).
Anchor: `allCoreTests msaaResolveScissor --gpu-check` (regression on `ca0568bd`).

### iOS BC texture support (#645)

**Classification:** KEEP decided by owner (iOS BC).

**Code locations:** `_sg_mtl_pixel_format`, `_sg_mtl_init_caps` (`[TrussC] BC` / `Enable BC on iOS` markers).

**Planned slice:** none; retain the current behavior.

Allow HAP on BC-capable iOS GPUs (trussc-mac #645 experiment): map BC1–BC7
under `@available(iOS 16.4, *)`, keeping 16.3 targets warning-free; register
sampling/filtering only when `supportsBCTextureCompression` is true. macOS BC
and iOS ETC/ASTC stay unchanged. trusscli's iOS preset defaults to 16.4; devices
without BC retain the existing HAP load failure. Local patch, no upstream PR planned.

### Metal swapchain continuation store actions (#191, #713)

**Classification:** KEEP essential.

**Code locations:** `_sg_mtl_begin_render_pass` (`Modified by tettou771`, depth/stencil store markers).

**Planned slice:** none; retain the current behavior.

Resumed swapchain passes use `SG_LOADACTION_LOAD` after Fbo/shadow/reflection work (#191);
upstream MSAA `MultisampleResolve` and depth/stencil `DontCare` lose that content.
In `_sg_mtl_begin_render_pass`, `RenderTarget::beginPass()` sets `action->depth.store_action == SG_STOREACTION_STORE`; this hint
selects `MTLStoreActionStoreAndMultisampleResolve` for MSAA color. Honor depth
and `action->stencil.store_action` via `_sg_mtl_store_action()` (#713 r2); resumable Fbo
and swapchain passes store/load stencil, initial passes keep configured clear.
This preserves masks across nested Fbo drawing and immediate `Texture::loadData()`.
Unhinted/default-DONTCARE passes retain upstream behavior. GL/WebGPU already
honor these actions; D3D11 only clears for CLEAR and retains storage at pass end.

### Metal synchronous mid-frame flush (#270)

**Classification:** KEEP essential.

**Code locations:** `sg_tc_mtl_flush`, `_sg_mtl_ensure_command_buffer`, `_sg_mtl_acquire_command_buffer`.

**Planned slice:** none; retain the current behavior.

Synchronous macOS/iOS Fbo readback must keep the frame, sgl recording and partial
swapchain unpresented. Between passes, `sg_tc_mtl_flush()` submits/waits for the
current buffer, clears it and lazily enqueues the next after the readback blit;
other backends provide a no-op with the same precondition.

- No commit listeners, `frame_index` advancement, deferred collection, uniform-slot
  rotation or offset/base reset; later uniforms append with normal growth/retirement.
- Reuse #599's ensure/acquire and unconditional final commit. Each sokol buffer
  acquires/releases one semaphore permit; flush waits for it and prior queue work,
  preventing later-frame uniform overwrite. Never signal manually; the separate
  synchronous readback blit is outside sokol's semaphore.
- Neither unmanaged pending drawables nor managed owner records present on flush;
  [managed presentation](#target-completion-capture-prefixes-and-managed-presentation-821864910)
  owns their completion contract.

`platform/metal/tcFboReadback.h` (#641) parks the swapchain and resumes LOAD/store
after its same-queue blit. Byte reads reject non-RGBA8 before touching render
state; `copyTo()`/`save()` stay synchronous. Anchors: `mtlFlush`, `fboReadback --gpu-check`;
Mac display/Metal validation and post-implementation review remain required.

### Metal uniform buffer growth (#859/#946)

**Classification:** KEEP decided by owner (R2).

**Code locations:** `_sg_mtl_resize_uniform_buffer`, `_sg_mtl_grow_uniform_buffer`, `_sg_mtl_apply_uniforms`, `_sg_mtl_begin_pass`, `METAL_UNIFORM_BUFFER_GROWN`.

**Planned slice:** none; retain the current behavior.

Upstream fixes `sg_desc.uniform_buffer_size` at `sg_setup()` (4 MiB, ~8k draws);
overflow `SOKOL_ASSERT` disappears in release, silently corrupting uniforms. R2 growth makes
`WindowSettings::reserveUniformBuffer` an optimization, not a correctness need.

- `_sg_mtl_grow_uniform_buffer()` doubles the current rotate slot, defers old
  storage release, resets `cur_ub_offset` and logs `METAL_UNIFORM_BUFFER_GROWN`
  (added to `_SG_LOGITEM_XMACRO`).
  `_sg_mtl_apply_uniforms()` checks before copying. First pass catches up smaller
  rotate slots to `ub_size` after acquiring their semaphore. `<stdio.h>` supplies log snprintf.
- Pending-draw correction #859 binds buffer/offset only to the uploaded vertex,
  fragment or compute block. No blanket zero bindings at pass start/growth:
  earlier blocks retain their generation through repeated growth/prefix submits,
  using existing deferred release without copying or a new pool policy.
- `_sg_mtl_resize_uniform_buffer()` allocates before retirement/replacement.
  Allocation/retirement-slot failure preserves storage/cursors and rejects the
  draw/catch-up pass with buffer-creation Error. Checked doubling/aligned int
  arithmetic (`<limits.h>`) rejects unrepresentable uploads before writes;
  existing growth Warning/upload statistics remain.

Anchors: `metalUniformGrowth` (PBR raster replay, compute/rollback wrapper),
`test_backend.py` (production Objective-C mocks, not native pixels/lifetime).
Air `MTL_DEBUG_LAYER=1` and base/head render-regression checks remain required.
WebGPU owns its separate growth entry; Vulkan uniforms remain upstream.

### Metal upload-only synchronization (#332/#599)

**Classification:** KEEP essential.

**Code locations:** `_sg_mtl_acquire_command_buffer`, `_sg_mtl_ensure_command_buffer`, `_sg_mtl_commit` (`[TrussC modification]`).

**Planned slice:** none; retain the current behavior.

Offscreen/upload-only ticks still end frames (#332): commit must enter the
in-flight semaphore before rotating uniforms or collecting resources.
`_sg_mtl_ensure_command_buffer()` shares first-pass creation/wait/completion
signal with `_sg_mtl_commit()`, which unconditionally ensures/commits even
without a pass or after #270 flush. Existing pass buffers/permits are reused;
retained/unretained-reference settings stay respected. Present unmanaged
`pending_drawables` and ready managed drawables once between ensure/commit; unready owners survive
foreign commits, and acquisition never depends on either list. Only real commit
collects, rotates/resets uniforms and advances the frame; flush does none of them.

### Target completion, capture prefixes and managed presentation (#821/#864/#910)

**Classification:** KEEP essential.

**Code locations:** `sg_tc_submit_prefix`, `sg_tc_mtl_command_buffer`, `sg_tc_mtl_register_drawable`, `sg_tc_mtl_authorize_drawable`, `sg_tc_mtl_cancel_drawables`, `_sg_mtl_end_pass`, `_sg_mtl_commit`; `util/sokol_gl_tc.h` (`sgl_tc_context_target_owned`, `sgl_tc_context_complete`, `sgl_tc_context_dependencies`).

**Planned slice:** none; retain the current behavior.

These `[TrussC]` functions (plus `sapp_tc_gl_make_current`) are internal framework
seams, not app API or upstream replacements. Reapplied at e7a48ed6 with prefix
submission and synchronous flush; flush, prefix and capture-buffer access finish
any open upstream blit encoder, sharing timed acquisition/final commit.

- `sgl_tc_context_target_owned()` selects explicit CPU completion: device commits
  invalidate upload epoch/cache; `sgl_tc_context_complete()` rewinds only its
  argument. Pool return/teardown complete their own contexts; ordinary sokol-gl
  retains auto-rewind. `sgl_tc_context_dependencies()` visits unconsumed pipeline,
  view and sampler references for host retirement, including shader/image dependencies.
- Metal registration retains drawable/depth/MSAA generations with stable owner ID,
  generation and frame serial, preserving saved swapchains across resize/foreign
  commits. Parking/end-pass leaves records unready; only final owner completion
  authorizes that exact frame. Stale/duplicate authorization fails. Commit presents
  ready records once; unready survive; close/discard cancels owner records.
  Unmanaged drawables retain automatic end-pass/commit behavior.
- End-pass completion retains immutable attachments through native completion,
  even after cancellation with unretained-reference buffers. Completion threads
  never touch mutable owner records; registration/authorization/cancellation/
  scheduling stay on the renderer thread.
- `sg_tc_submit_prefix()` requires no open pass; no presentation, listeners,
  frame advancement or uniform/stream rotation. Metal preserves uniform slot/
  offset and next buffer takes its normal permit; GL uses `glFlush`, D3D11 Flush.
  Synchronous capture parks the pass then submits. Async Metal staging encodes
  its blit through `sg_tc_mtl_command_buffer()` and submits before source overwrite.
  `sg_tc_mtl_flush()` remains synchronous between-pass Fbo submission.
- #884 capture acquisition bounds the shared semaphore wait to ten seconds.
  macOS synchronous window capture also bounds completion to ten seconds, owns
  late results and leaves the pass parked on failure instead of retrying throttle.
- GLX nested-window execution restores the drawable with `sapp_tc_gl_make_current()`;
  the host parks outgoing passes and resumes incoming attachments with LOAD.

Part 1 installed no completed-frame snapshot/per-window shadow allocation.
`windowCaptureSource`/`finishWindowCaptureSource` and `shadowStorageForWindow`
remain host interfaces; owner retired Q2's snapshot reservation, so no future copy
is promised. Existing capture sources/presentation-time limits remain. Later
implementation checks require native Metal/API, Win32 debug-layer and Pi V3D pixels.

### Reserved GL swapchain destination hook (#821/#864)

**Classification:** DELETE unused (planned).

**Code locations:** `sg_tc_gl_resolve_swapchain`, `_sg_gl_end_render_pass` (`snapshot_source`, `snapshot_destination`), `_sg_gl_begin_resolve`.

**Planned slice:** R5 GL Q2 snapshot hook retirement.

`sg_tc_gl_resolve_swapchain()` supplies an explicit end-pass resolve destination,
sharing V3D detection/Notice/pre-first-resolve flush and full-extent scissor
handling. It restores READ/DRAW bindings and source read buffer; no separate
platform resolve/`glFinish`. No snapshot/shadow allocation was installed.
R5 deletes the unused function and Q2 destination branch, restoring upstream's
end-render-pass signature. Ordinary attachment/scissor/V3D resolves, prefix
submission, managed Metal drawables and live capture seams belong to their entries.

### Mesa V3D pre-resolve flush and activation Notice (#858)

**Classification:** KEEP decided by owner (V3D flush).

**Code locations:** `_sg_gl_setup_backend` (`Native Pi 5` renderer-prefix marker), `_sg_gl_begin_resolve`, `_sg_gl_backend_t.v3d_resolve_flush`, `_SG_GL_FUNCS`, `TRUSSC_GL_V3D_RESOLVE_FLUSH`; tcGlobal.cpp `internal::sokolLog`.

**Planned slice:** no current removal slice; conditional future V3D removal only at the stated Mesa baseline.

keep for now; remove once Mesa < 25.1.5 is legacy on supported Raspberry Pi OS

Owner-supplied Pi 5 / Raspberry Pi OS trixie evidence: Mesa 25.0.7-2+rpt4,
V3D 7.1.10.2 still needs the flush; this slice performed no version/device probe.
Exact `GL_RENDERER` prefix `V3D ` (space included) enables native `V3D 4.2` /
`V3D 7.1.10.2`, excluding wrapped names such as
`zink Vulkan 1.3(V3D 4.2.14.0 (MESA_V3DV))`. Each resolving raster pass calls
`glFlush()` once before its first resolve regardless of draws/load actions;
all blits execute, no-resolve passes add no flush.

Win32's desktop-GL loader adds `glGetString`/`glFlush` to `_SG_GL_FUNCS` and
`GL_RENDERER` (`0x1F01`); generated pointers use existing WGL/opengl32 lookup.
Other paths use platform headers. Historical #858 round-2 checks covered Win32,
GLX/EGL, both Android header branches and Emscripten; macOS GL was source/API only.
The activation log `SG_LOGITEM_TRUSSC_GL_V3D_RESOLVE_FLUSH` maps through
`internal::sokolLog` to one RenderTarget Notice per process, including reinit
(atomic in tcGlobal.cpp). The bridge supplies the exact release-build message;
other informational items stay Verbose. No query API, header-inline mutable
state or Mesa version check.

### WebGPU checked uniform growth (#859/#863, slimmed #1097/#1101)

**Classification:** KEEP decided by owner (R2).

**Code locations:** `_sg_wgpu_uniform_reserve`, `_sg_wgpu_uniform_retire_groups`, `_sg_wgpu_uniform_bindgroup`, `_sg_wgpu_uniform_fail`, `ub_generation`, `WGPU_UNIFORM_BUFFER_GROWN`, `WGPU_UNIFORM_UPLOAD_FAILED`.

**Planned slice:** none; retain the current behavior.

R2 retains transactional storage growth and generation-based uniform groups;
e7a48ed6 retains upstream `d556445a`'s compact `bgl_index` mapping for sparse
slots, while TrussC snapshots binding numbers for each generation.

- `_sg_wgpu_uniform_reserve()` checks wide arithmetic, device limit and uint32
  dynamic offsets before CPU/GPU allocation. Keep 64 KiB binding extent/device
  alignment (WebGPU 256 bytes); TrussC starts at 1 MiB, explicit reserves apply.
- Growth copies staging without moving block offsets, uploads the old recorded
  prefix, then releases its backend reference without destroying retired buffers.
  Encoders/commands/groups retain resources through queue completion. Retire
  old groups (idle shaders too), lazily replace by generation; split-stage draws
  retain valid offsets into copied data.
- Synchronous allocation failure leaves storage unchanged; one Error suppresses
  uploads/draws/dispatches until commit, with next-frame retry. One
  `WGPU_UNIFORM_BUFFER_GROWN` Warning per backend init; `WGPU_UNIFORM_UPLOAD_FAILED`
  is the other added `sg_log_item`. No Metal/Vulkan/API change.
- #1097/#1101, owner 2026-10-29: asynchronous OOM/validation uses WebGPU reporting;
  no error scopes, per-generation callbacks or last-good fallback. Non-null error
  buffers/groups can drop/corrupt a frame; current storage persists without recovery.

Anchor: `webgpuUniformGrowth` Release browser raster/compute pixels and console
verdict (`run.py --build`); native production mock uses installed emdawn headers
for growth/prefix/generation/retention and synchronous failure checks.

### WebGPU bind-group failure handling (#863/#1097/#713)

**Classification:** KEEP essential.

**Code locations:** `_sg_wgpu_create_bindgroup`, `_sg_wgpu_bindgroups_cache_t`, `_sg_wgpu_apply_bindings`, `bindgroup_error_logged`.

**Planned slice:** none; retain the current behavior.

Upstream `6a7d3ee7` absorbed valid-only insertion and FAILED-group discard;
residual local behavior is cache-disabled valid-only draw suppression and
once-only `bindgroup_error_logged`. Default direct-mapped cache is 1024 slots
(`Bound (cache)`): never cache null, release failed native pool slots, evict
normal collisions without Warning/loss counter. Representation/device arithmetic
bounds and `Bound` annotations remain with this feature.

### Core integration and pool configuration (not vendor patches)

Integration context, not vendor-header deltas: ShaderStream uploads each flush
before binding a dedicated transient buffer pair, then selects another pair.
Persistent Texture/atlas updates stage through `sg_copy_buffer_to_image`, parking/
resuming passes and consuming lazy window prefixes before copy. The once-per-frame
image update guard is removed; WebGPU GPU buffers/256-byte row padding order copies
between encoded draws (upstream CPU staging uses QueueWriteTexture before submit).
Check signed buffer-size/row/slice bounds before allocation; immutable mips are
contiguous mip-major data. Addon Q3/Q4 migration is complete (see its linked ledger).

`tcGlobal.cpp` `activeGraphicsPools`/`sg_desc` supply configurable fixed setup
budgets: default shader/pipeline 1024 each (upstream 32/64), buffer/image/view/
sampler 10000; no post-setup growth. Integration sgl context reserve 64 differs
from standalone initial 8.

## util/sokol_gl_tc.h (forked from upstream util/sokol_gl.h)

### Write-transient flush buffers and layer upload reuse (#713 Q1)

**Classification:** KEEP essential.

**Code locations:** `_sgl_prepare_flush_buffer`, `_sgl_draw`, `_sgl_release_gpu_buffers`, `flush_buffers`, `upload_*` cache (`[TrussC fork]`).

**Planned slice:** none; retain the current behavior.

Multiple flushes per device frame (Fbo suspend/resume, layers, rewind) need
separate write-transient storage for changed vertices. Per-context pools grow
and reuse after `sg_commit()`; sokol renames in-flight storage. Context reset
keeps the device-frame cursor; release/destruction frees the pool. Unchanged
layers share `upload_*` cache, avoiding N-layers × V-vertices allocation.
Buffers grow to CPU capacity at offset zero, bounded by `INT_MAX / sizeof(_sgl_vertex_t)`;
if doubling exceeds it, use the representable requested count. Prior flush contents
survive the device frame. Depth-NONE pipelines disable writes/compare ALWAYS for
upstream validation (core target pipelines do likewise).
Anchors: `sglLayerUpload`, `shaderStreamOverflow`, `textureUploadGuard --gpu-check`.

### CPU vertex/uniform/command growth

**Classification:** KEEP decided by owner (R2).

**Code locations:** `_sgl_checked_capacity`, `_sgl_next_vertex`, `_sgl_next_uniform`, `_sgl_next_command`, `_sgl_prepare_flush_buffer`, `_SGL_DEFAULT_MAX_VERTICES`, `_SGL_DEFAULT_MAX_COMMANDS`.

**Planned slice:** none; retain the current behavior.

Double CPU vertex/uniform/command capacity on overflow, preventing dropped
rendering. Resize an unused GPU flush-pool slot to CPU vertex capacity as needed;
sokol retains old backend storage through in-flight work. Metal starts at
128 vertices/64 commands; non-Metal at 64K/16K; all retain growth.

### Float vertex colors

**Classification:** KEEP decided by owner (R4).

**Code locations:** `_sgl_vertex_t.rgba`, `_sgl_context_t.rgba`, `_sgl_init_pipeline`, `sgl_c*` color setters.

**Planned slice:** none; retain the current behavior.

R4 uses `float[4]` in vertex/context rgba and `SG_VERTEXFORMAT_FLOAT4` instead
of `uint32_t`/`SG_VERTEXFORMAT_UBYTE4N`: 1/255 quantization prevented smooth trail/afterimage fade to zero.
Color setters (`sgl_c*f/c*b/c1i`) store floats directly; `_sgl_pack_rgbab()` /
`_sgl_pack_rgbaf()` were removed. Vertices grow 28→40 bytes (~1.4×), preserving
values above 1.0 for float Fbos. Packed-color rollback is canceled.

### Context pool growth and all-or-nothing pipelines (#317)

**Classification:** KEEP decided by owner (R2 growth); KEEP essential (pipeline/invalid-ID safety).

**Code locations:** `_sgl_grow_context_pool`, `_sgl_try_malloc`, `_sgl_try_malloc_clear`, `_sgl_alloc_context`, `_sgl_make_pipeline`, `_sgl_init_pipeline`, `_sgl_draw`, `_SGL_DEFAULT_CONTEXT_POOL_SIZE`.

**Planned slice:** none; retain the current behavior.

Pool/allocation failure returns id 0 for existing caller handling; R2 doubles
contexts as it does buffers. `_sgl_init_pipeline()` stops at the first invalid
`sg_make_pipeline()` ID or non-`SG_RESOURCESTATE_VALID` pipeline; `_sgl_make_pipeline()` destroys prior pipelines, frees
the sgl slot and returns `SG_INVALID_ID`. `_sgl_draw()` skips id-0 commands
(recorded with invalid/destroyed pipeline) before `sg_apply_pipeline()`.

`_sgl_grow_context_pool()` doubles array/bookkeeping up to `_SGL_MAX_POOL_SIZE - 1`,
relooks up `_sgl.cur_ctx` (contexts are ID-referenced) and logs old/new sizes via
`SGL_LOGITEM_CONTEXT_POOL_GROWN`, `_sgl_log_msg()`/snprintf (`<stdio.h>`).
Fallible temporary allocations roll back, preserving old pool/current context;
creation returns 0 so TrussC can warn once without retry. Initial reserve grew
4→8 for default/secondary/Fbo contexts. The adjacent fixed ~13 KB matrix-stack /
~100 KB pool estimate is historical: #331 made stacks dynamic, not fixed budgets.
Anchor: `core/tests/sglPoolLimits`.

### Growable matrix stacks (#331)

**Classification:** KEEP decided by owner (R2).

**Code locations:** `sgl_push_matrix`, `_sgl_context_t.matrix_stack`, `_sgl_destroy_context`, `sgl_tc_reset_matrix_stacks`.

**Planned slice:** none; retain the current behavior.

Each context owns a heap array per matrix mode, starting with only the base
identity matrix. `sgl_push_matrix()` doubles its mode's capacity as needed,
copies the live matrices through the configured allocator; growth is silent.
Context destruction frees all three arrays. No fixed nesting limit
remains; underflow is unchanged. Allocation/representable-capacity failure
keeps the existing stack and reports out of memory plus `stack_overflow`.
The fixed pipeline stack still uses `stack_overflow`, so that public flag stays.
The shared screen/Fbo warning now describes dropped draws and these remaining
causes; target completion already reports and clears Fbo context errors.

### Explicit custom shader layouts (#869)

**Classification:** KEEP decided by owner (R4).

**Code locations:** `_sgl_init_pipeline` (`[TrussC] Custom shaders` marker), `_sgl_vertex_t`.

**Planned slice:** none; retain the current behavior.

Preserve caller `sg_pipeline_desc.layout` when a custom shader supplies valid
`layout.attrs[0].format`; no new field/API. Built-in shader/context defaults and
custom shaders without explicit layout retain pos/uv/float-color/psize layout.
Explicit layouts describe the existing sgl buffer; TrussC premult/coverage use
attributes 0–2, stride 40, omitting unused point-size declarations on every backend
to avoid GL warnings. Anchor: `sglVertexLayout`; native GLCORE/GLES3 warning/pixel
and render-regression checks cover rendering.

### sgl rewind/reset/release/ensure seams

**Classification:** KEEP essential (live context seams); DELETE unused (planned) for sgl_tc_draw_rewind only.

**Code locations:** `sgl_tc_draw_rewind`, `sgl_tc_context_draw_rewind`, `sgl_tc_context_has_pending_draws`, `sgl_tc_context_reset`, `sgl_tc_context_release_buffers`, `sgl_tc_context_ensure_buffers`, `sgl_tc_reset_matrix_stacks`, `_sgl_draw_rewind`, `_sgl_reset_buffers`.

**Planned slice:** R5 deletes only the unused all-layer wrapper; other seams remain.

TrussC additions, absent upstream:

| Function | Purpose |
|---|---|
| `sgl_tc_draw_rewind()` | R5 unused all-layer wrapper. Historically flushed all layers and dirtied matrices/uniforms for same-frame Fbo suspend/resume; context-specific rewind now supplies that live use. |
| `sgl_tc_context_draw_rewind(ctx)` | Rewind that context only. |
| `sgl_tc_context_has_pending_draws(ctx)` | Query unconsumed recording. |
| `sgl_tc_context_reset(ctx)` | Reset counters; externally test-only but retained, outside R5. |
| `sgl_tc_context_release_buffers(ctx)` | Release idle CPU/GPU buffers, retaining context shell/pipelines. |
| `sgl_tc_context_ensure_buffers(ctx)` | Allocate after release before drawing; no-op if allocated. |
| `sgl_tc_reset_matrix_stacks()` | Reset current context stacks at `present()` (#232) and `Fbo::beginInternal()` (#327), preventing unpopped pushes carrying into the next frame/pass. |

Context rewind/completion/release/ensure are live target-management seams;
[managed presentation](#target-completion-capture-prefixes-and-managed-presentation-821864910)
owns completion/dependency visitation. Only the all-layer wrapper is retired.

## Superseded and moved history

Historical reasons/owners; use the introductory preservation policy.

- **CGBitmapInfo** (`d16a7231`): former `_sapp_macos_set_dock_tile` cast CGImageAlphaInfo/CGImageByteOrderInfo before OR, avoiding C++20 `-Wdeprecated-enum-enum-conversion`. Gone; current NSBitmapImageRep is `sapp_bind_mouse_cursor_image`, not Dock conversion.
- **SetForegroundWindow:** already moved to `tcPlatform.h`/macOS, Windows and Linux bringWindowToFront; historical `trussc::` is now `tc`.
- **Fixed clipboard/path/count slots:** [R1 growth](#growable-dropclipboard-storage-and-native-transfer-ownership-239-1023-866-878) owns removed capacities/obsolete diagnostics.
- **Session freeze/layered ending:** [native close/quit](#native-close-quit-and-async-dialog-preflight-239-266-1117) owns #239 C2/C3 / `020692e4` and retained reverse ending.
- **Append uploads (L10):** [Q1 flush pools](#write-transient-flush-buffers-and-layer-upload-reuse-713-q1) replace append storage, preserving changed-upload isolation/layer reuse.
- **GL scissor absorption:** [whole-attachment resolves](#whole-attachment-msaa-resolve-and-scissor-861870) owns upstream `37e21167` and residual helper users.
- **WebGPU valid-only/FAILED absorption:** [bind-group failure handling](#webgpu-bind-group-failure-handling-8631097713) owns upstream `6a7d3ee7` and residual behavior.
- **sapp_get_swapchain/SBGRA8:** [compatibility sync](#upstream-environmentswapchain-compatibility-713-q5q6q7) owns renamed APIs/formats/depth; [integration](#core-integration-and-pool-configuration-not-vendor-patches) owns completed core/addon write/copy migration.
- **sgl_tc_get_texture (#1386 1b):** owner-confirmed removal; TrussC texturing restoration is a separate product choice.
- G1–G11/K9/L10 label the descriptive entries above. #858 numerical validation belongs in its implementation PR, not this ledger slice's results.

## How to Update Sokol

For patched copies (`sokol_gfx.h`, `sokol_glue.h`, `util/sokol_gl_tc.h`,
`addons/tcxImGui/src/sokol_imgui.h`), use a three-way merge into scratch output.
Do not overwrite them and reconstruct local patches manually.

`sokol_app_tc.h` stays a permanent fork. Review upstream improvements selectively
against native equivalents and the feature invariants above. Singleton sharing
does not make it an automatic three-way target. Keep its original lineage and
compatibility-sync revision separate from each patched copy's upstream base.

### Recommended: three-way merge for patched copies

Use local checkouts only. Stage BASE from the recorded upstream revision, OURS
from the current TrussC copy, THEIRS from the intended local upstream checkout:

```bash
# Run from TrussC; set the local upstream checkout and this file's base.
sokol_clone=/path/to/local/sokol
base_sha=e7a48ed6
merge_dir=$(mktemp -d)
git -C "$sokol_clone" show "${base_sha}:sokol_glue.h" > "$merge_dir/BASE.h"
cp "$sokol_clone/sokol_glue.h" "$merge_dir/THEIRS.h"
cp core/include/sokol/sokol_glue.h "$merge_dir/OURS.h"

# Conflicts appear as <<<<<<< / ======= / >>>>>>> in the scratch result.
# Plain merge-file works on Git 2.43 (which rejects --diff-algorithm).
# On newer Git, optionally add --diff-algorithm=histogram.
git merge-file -p "$merge_dir/OURS.h" "$merge_dir/BASE.h" "$merge_dir/THEIRS.h" > "$merge_dir/MERGED.h"

# Resolve and check every affected feature before copying back.
cp "$merge_dir/MERGED.h" core/include/sokol/sokol_glue.h
```

Repeat for `sokol_gfx.h`; for `util/sokol_gl_tc.h` use upstream
`util/sokol_gl.h` as BASE/THEIRS. The addon copy uses upstream
`util/sokol_imgui.h` and its own linked modification ledger.

Resolve complete marked feature blocks using the introductory preservation
policy and each entry's conditional gates. When a slice lands, update its entry
and retain its historical reason to prevent reapplication.

### Direct overwrite and post-merge checks

Only unmodified headers such as `sokol_log.h` may be overwritten directly.
After updating:

1. Record the upstream base separately for each file actually migrated; update
   app's compatibility revision only for a deliberate sync, preserving lineage.
2. Record absorbed upstream behavior and the residual local delta. Search
   `rg -n 'tettou771|Modified by|\[TrussC' core/include/sokol` to review complete
   affected blocks; preserve every existing attribution/marker.
3. Run affected feature tests, every tools test and the four native-window
   contract commands, comparing failures with origin/main. Recheck definitions
   for every new/renamed Runtime/TrussC.h sapp call on all reachable platforms.
4. Perform the implementation slice's native checks: macOS/Metal, Win32 D3D11,
   X11 GLX, Raspberry Pi EGL/V3D, iOS, Android and WebGL2/WebGPU as affected.
   Source/mocked tests do not substitute for required device/SDK verification.
5. Regenerate checked API outputs using the repository workflow; use strict
   reference validation if api-reference.toml changed.

## Author

All modifications by tettou771
