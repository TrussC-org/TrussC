# TrussC Architecture

## 1. Concept & Philosophy

**"Thin, Modern, and Native"**

A lightweight creative coding environment optimized for the AI-native and GPU-native era, suitable for commercial use.

| Principle | Description |
|-----------|-------------|
| **Minimalism** | Select only necessary features. No bloat. |
| **License Safety** | MIT / Zlib / Public Domain only. No GPL contamination. |
| **Transparency** | Thin wrappers over OS native APIs. Access to Metal / DX12 / Vulkan when needed. |
| **AI-Native** | Standard, predictable API design (C++20) that AI can easily generate code for. |

**Namespaces:**
- `tc::` - Core & Official Modules
- `tcx::` - Community Addons / Extensions

### How design decisions are made

When a change can take more than one reasonable shape, these rules decide it.

- **Think of three kinds of apps.** TrussC apps are roughly:
  1. personal projects, where trying things quickly matters most;
  2. long-running installations (exhibitions), where maintenance comes first: recovering with nobody on site, logs, predictable behavior;
  3. apps distributed to others, which run on other people's machines, so safety and integrity come first.

  A convenience that puts any one of these at risk is not adopted, however useful it is. If it adds no security risk to any of them, it may be adopted for convenience.
- **Simple and predictable.** Prefer the design that can be stated in one sentence. Count complexity as a cost, even when it brings an implementation benefit. Options and automatic behavior are added later, when there is a real need.
- **Follow existing conventions.** Before adding a mechanism, look for code that already does the same thing, and do it the same way.
- **Fix the code, not the docs.** When the code is wrong, fix the code. A documented workaround is only a stopgap until the fix lands.
- **No arbitrary limits.** A size or count limit needs a concrete reason, such as a file format's own limit or the range the code can represent. Don't add one just to be safe.
- **People decide behavior and API changes.** Choices that change behavior or the API are made by a person. AI agents may lay out the options and recommend one, but they don't make the call.

---

## 2. Tech Stack

Hybrid composition of native wrappers and high-quality lightweight libraries. See [BUILD_SYSTEM.md](BUILD_SYSTEM.md) for CMake details.

| Category | Library / Strategy | License |
|:---------|:-------------------|:--------|
| **Window/Input** | sokol_app | zlib |
| **Graphics** | sokol_gfx (Metal/DX12/Vulkan) | zlib |
| **Shader** | sokol-shdc (GLSL → Native) | zlib |
| **Math** | In-house (C++20 template) | MIT |
| **UI** | Dear ImGui (tcxImGui addon) | MIT |
| **Image** | stb_image, stb_image_write | Public Domain |
| **Font** | stb_truetype + fontstash | Public Domain |
| **Audio** | sokol_audio + dr_libs | zlib/PD |
| **Video/Camera** | OS Native (AVFoundation / Media Foundation) | - |
| **Serial** | OS Native (POSIX / Win32) | - |
| **Build** | CMake | - |

---

## 3. Directory Structure

```
MyProject/
├── CMakeLists.txt       # Build definition (shared template)
├── addons.make          # Addons to use (user edits this)
├── src/
│   ├── main.cpp         # Entry point
│   ├── tcApp.h          # User app definition
│   └── tcApp.cpp        # User app implementation
├── bin/
│   └── data/            # Assets (images, fonts, etc.)
└── icon/                # App icon (PNG, auto-converted)
```

---

## 3.5 Project Generation & TRUSSC_DIR

### How trusscli Works

The `trusscli` tool (TrussC Project Generator) creates new projects by:
1. Copying template files (CMakeLists.txt copied as-is, no modification)
2. Generating `CMakePresets.json` with OS-specific configuration
3. Generating IDE-specific files (.vscode/, Xcode project, etc.)

### TRUSSC_DIR Strategy

**Design Principle:** CMakeLists.txt is never modified. All project-specific configuration goes into CMakePresets.json.

**Why relative vs absolute path:**

| Project Location | Path Type | Reason |
|------------------|-----------|--------|
| Inside TrussC repo (examples) | Relative (fallback) | Examples move WITH trussc. Moving the entire repo keeps things working. |
| Outside TrussC repo (user projects) | Absolute | User projects move INDEPENDENTLY. trussc location is typically fixed, but users may relocate their projects. |

**Template CMakeLists.txt fallback:**
```cmake
if(NOT DEFINED TRUSSC_DIR)
    set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
endif()
```

**Generated CMakePresets.json (user project):**
```json
{
  "cacheVariables": {
    "TRUSSC_DIR": "/absolute/path/to/trussc"
  }
}
```

This ensures:
- Examples work when you move the entire TrussC repo
- User projects work when you move just the project folder
- No complex relative path calculations needed

### Windows Subsystem: console tools vs GUI apps

On Windows every executable is stamped with a **subsystem** that decides how it
attaches to a launching terminal. A `WINDOWS` (GUI) image detaches immediately —
`cmd` returns to the prompt, closing the console window doesn't kill it and sends
no `CTRL_CLOSE` — while a `CONSOLE` image keeps its terminal (stdout is visible,
`Ctrl+C` / window-close reach it).

- **GUI apps (default).** In Release, `TrussC.h` emits
  `#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")` so a
  double-clicked app never flashes a console. The pragma fires from the **app's
  own** translation units.
- **Console tools.** Define `TRUSSC_SHOW_CONSOLE` (in `local.cmake`, which
  survives `trusscli update`) to suppress that pragma in the app's TUs, leaving
  the default console subsystem — how `trusscli` itself builds.
  ```cmake
  # local.cmake
  if(WIN32)
      target_compile_definitions(${PROJECT_NAME} PRIVATE TRUSSC_SHOW_CONSOLE)
  endif()
  ```

**Why libraries must not carry the pragma.** A `#pragma comment(linker, ...)` in
a *library* object (core TrussC or an addon) rides into the final link via that
object's `.drectve` section, and for `/subsystem` a `.drectve` directive
**overrides the command-line `/SUBSYSTEM`**. So a single GUI-header-including
addon (e.g. `tcxWebSocket`) would silently turn a console tool back into a GUI
app. To prevent that the build defines `TRUSSC_LIBRARY_TU` (PRIVATE) on the core
lib and on every addon target, and the pragma is gated on
`!defined(TRUSSC_LIBRARY_TU)`. Net effect: the subsystem is decided **only by the
app's own TUs** — GUI by default, console when the app defines
`TRUSSC_SHOW_CONSOLE` — no matter which addons it links.

---

## 4. API Design

### User Application

```cpp
// tcApp.h
#pragma once
#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
};
```

### Entry Point

```cpp
// main.cpp
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 1024;
    settings.height = 768;
    settings.title = "My TrussC App";

    runApp<tcApp>(settings);
    return 0;
}
```

### Global Helper Functions

```cpp
namespace tc {
    // Window
    int getWidth();
    int getHeight();
    void setFullscreen(bool full);
    void toggleFullscreen();

    // Mouse
    float getMouseX();
    float getMouseY();
    bool isMousePressed();

    // Time
    double getElapsedTime();
    double getFrameElapsedTime();
    double getDeltaTime();
    uint64_t getFrameNum();

    // Loop Control
    void setFps(float fps);
    void redraw();
}
```

---

## 5. Core Architecture

### A. Loop Modes

TrussC supports flexible frame rate control with synchronized and independent modes.

**Special Constants:**

| Constant | Value | Behavior |
|----------|-------|----------|
| `tc::VSYNC` | -1.0f | Sync to monitor refresh rate |
| `tc::EVENT_DRIVEN` | 0.0f | Only on explicit `tc::redraw()` call |

**Synchronized Mode (Default):**

```cpp
tc::setFps(VSYNC);       // VSync (default)
tc::setFps(60);          // Fixed 60fps
tc::setFps(EVENT_DRIVEN); // Event-driven (power saving)
```

**Independent Mode:**

```cpp
tc::setIndependentFps(120, VSYNC);  // Update 120Hz, draw VSync
tc::setIndependentFps(60, 30);      // Update 60Hz, draw 30fps
```

**Note:** In event-driven mode, the app doesn't freeze. Event handlers still fire normally.

**Timing rules:**

- **One clock.** `getElapsedTime()` (double), `getElapsedTimef()`, `getElapsedTimeMillis()` and `getElapsedTimeMicros()` read one `steady_clock` whose origin is taken at program start. `resetElapsedTimeCounter()` only restarts what these getters report; framework timing (Node timers, the loop, `ScreenRecorder`, the `tc_get_health` uptime) runs on the underlying clock and is never reset. `getFrameElapsedTime()` is the same value sampled once per frame, so every update step and the draw of a frame agree. `getElapsedTimef()` is a float that loses precision after about a day: use it for animation, and the double where precision matters. Because the counter can be reset, measure durations with `getSystemTimeMicros()` differences taken as `int64_t`.
- **Delta time.** In VSYNC and `setFps()` modes `getDeltaTime()` is the measured time since the previous update. With a fixed update rate (`setIndependentFps(120, VSYNC)`, and `runHeadlessApp`) update runs as fixed steps and every step reports exactly `1 / updateFps`.
- **Bounded catch-up.** A fixed-rate update runs at most `setMaxUpdateSteps()` steps per frame (default 10). Time beyond that (after a stall, when `update()` is slower than its own rate, or when the update rate is more than that many times the display rate) is dropped with a one-time warning, instead of freezing the app while it replays. `runHeadlessApp` applies the same cap per loop pass. Between passes it sleeps only until the next step is due (at most 1 ms), on a high-resolution timer on Windows, where a plain sleep rounds up to ~15.6 ms, so a fast headless rate stays within the cap per pass. `setMaxUpdateSteps(0)` (or less) removes the cap when every step must run, e.g. a deterministic simulation; a long stall is then replayed in full. `getFrameRate()` reports the measured rate, so it shows when that happens (fixed steps are counted by the time they consumed, which keeps the value steady when the update rate isn't a multiple of the frame rate).
- **Switching modes at runtime** (`setFps()` / `setIndependentFps()`) starts the new rate from the moment of the switch; time spent in the previous mode is not replayed, neither as fixed steps nor as the first measured delta. Called between updates (a key handler, `draw()`), the next delta counts from the call: after an hour of `EVENT_DRIVEN`, the first `setFps(VSYNC)` update doesn't report the hour (so Node timers don't all fire at once), but the wait for the first `redraw()` after switching to `EVENT_DRIVEN` counts. Called inside an update (including a `setup()` that runs in one), it counts from that update's start, so work in that update counts whether it comes before or after the call. A `setup()` that runs in a `draw()` instead (a node added during `draw()`, or the App when it starts with `setIndependentFps(EVENT_DRIVEN, …)`; with `setFps(EVENT_DRIVEN)` the first frame runs an update) counts as between updates. So a switch between updates drops the time since the last update only when the update mode really changes into a measured one (VSYNC or `setFps()`). In the usual modes that is under a frame; it is long only when the previous mode ran no update for a while, like an `EVENT_DRIVEN` idle. Changing only the draw rate drops nothing; switching between a synced update (`setFps()`) and an independent one (`setIndependentFps()`) counts as a change of the update mode even at the same rate (`setFps(VSYNC)` to `setIndependentFps(VSYNC, 30)` drops up to a frame). Entering a fixed update rate restarts with one step: the first frame after the switch runs one fixed update step (and draws at a fixed draw rate), which can count more or less than the time since the last update (from a 144 Hz display to a fixed 60 Hz, the step is ~9.7 ms longer than the 1/144 s since the last update). Calling them again with the current rates does nothing, so `setFps(guiValue)` every frame is fine; on the frame where the value changes, a call from `draw()` makes that update's delta count only from the call, shorter than the frame.
- **Fixed draw rates** skip display frames with a half-frame tolerance: a target at or just above the display rate (`setFps(60)` on a 59.94 Hz display) draws every frame, and integer ratios (60 on 120 Hz, 30 on 60 Hz) draw every other frame. `Window::setFps()` throttles secondary windows the same way.
- **Every window shares the tick implementation.** Each host-owned WindowContext has its scheduler, steady-clock update time, frame sample, read-only FPS samples and update/draw counters. Native callbacks supply per-window tick sources and visibility. Timers created in update listeners run inside that window’s update scope (#307); event/draw callbacks use its measured clock. `getFrameCount()` equals `getUpdateCount()`; `getDrawCount()` counts real draws (zero headless), with separate internal device-frame guards.
- **Runtime ownership.** Host storage owns every open Window, the attachment ledger, constructor tokens, the activity gate and one epoch per run. Window handles do not close their targets when released. Attach Apps with `setApp<T>()`, replace them with another constructor call, or remove them with `destroyApp()`. Shutdown ends one reverse snapshot of attachments before disposing every native/GPU backing.
- **Process context has no Window.** A null selection resolves to the non-window context, never main or the first live Window. Both queues and maintenance run there once per safe outer turn, including zero-window, headless, hidden and no-draw turns. Nested native turns and App/lifecycle callbacks cannot reenter process services. Queue failures still permit maintenance. Entry/turn unwind restores the actual caller selection, its SGL context and its native GL display/context/draw/read surfaces, including an unbound caller. A root swapchain pass there is refused with one Warning directing drawing to a window callback or an Fbo; explicit offscreen targets work without importing another Window’s dimensions. Service turns use separate CPU drawing state with explicit event hubs for framework services. MCP Window tools accept positive IDs. Omission requires exactly one open Window; invalid or ambiguous requests return an error with the available IDs. Cleanup and unload notifications explicitly address their receivers.
- **Constructor ownership.** A host construction scope encloses the entire derived constructor. Each candidate receives its own token before its body runs. Nested construction restores the previous owner. Failed or cancelled unpublished candidates retire their callbacks without invented setup/exit. Published Apps use the common ending on close, swap, removal, headless shutdown and guest retirement.
- **Native creation is a transaction.** Desktop startup and secondary creation use the same descriptor data and per-backend constructor. Native identity, dimensions and resources exist before tentative registry insertion/binding; client readiness precedes show and first input. The shared publication transaction marks Constructing across bind/show, excludes ticks and physical destruction, revalidates native identity after synchronous calls and rolls back publication/resources once on rejection or exception. Sole-surface ports consume the same descriptor substrate with their existing capabilities.
- **A resized frame has one extent.** Absorb external size changes before begin-pass, including the first WM clamp after creation. Drawable acquisition and published framebuffer metadata agree before pass/viewport/clear setup. X11 GLX/EGL replace a resized native surface before rendering because a driver can report the new window size while retaining the old back buffer until swap. Surface replacement preserves the Window identity and restores the caller's actual draw/read owners; a failed renewal destroys the Window and orders backend exit, without recovery or further rendering through the failed drawable. Win32 modal ticks sample client dimensions and resize attachments before the client callback, just as ordinary ticks do. Existing capture wake/drain, target leases and afterFrame ordering are unchanged.

### B. Scene Graph & Event System

**Node Basics:**

- **tc::Node**: Base class with parent-child relationships and local transformation
- **Activation Control**: `isActive` stops node and all descendants completely
- **Visibility Control**: `isVisible` false hides the node and its whole subtree — no draw, no mouse hit test — while update continues (`isActive` false stops the subtree entirely)
- **Event Traverse**: Child nodes receive events even if parent has events disabled

**Event Dispatch (Internal):**

Events are automatically dispatched by `App::handle*` methods. The `Node::dispatch*` methods are **private** and accessed only via `friend class App`. User code should **never** call dispatch methods manually.

```cpp
// WRONG - Don't do this
void tcApp::mousePressed(Vec2 pos, int button) {
    dispatchMousePress(pos.x, pos.y, button);  // Compile error: private
}

// CORRECT - Just override and use
void tcApp::mousePressed(Vec2 pos, int button) {
    // Your custom logic here (dispatch happens automatically)
}
```

**RectNode - 2D UI Base:**

`RectNode` provides rectangle-based hit testing. Events are **disabled by default** - call `enableEvents()` to make a node touchable.

```cpp
class MyButton : public RectNode {
public:
    MyButton() {
        enableEvents();  // Required to receive events
        width = 120;
        height = 40;
    }

protected:
    bool onMousePress(Vec2 local, int button) override {
        // Handle click
        return true;  // Consume event
    }
};
```

**Key Points:**

| Property | Default | Description |
|----------|---------|-------------|
| `eventsEnabled_` | `false` | Call `enableEvents()` to receive events |
| `width`, `height` | `100.0f` | Hit area size (0 = no hit) |
| `isActive` | `true` | If false, node and children are completely disabled |
| `isVisible` | `true` | If false, draw and mouse hit test are skipped for the node and all its descendants; update keeps running |

**Event Handler Return Values:**

- Return `true` to **consume** the event (stop propagation)
- Return `false` to let the event **bubble** to other nodes

**Available Event Handlers (override in subclass):**

```cpp
bool onMousePress(Vec2 local, int button) override;
bool onMouseRelease(Vec2 local, int button) override;
bool onMouseMove(Vec2 local) override;
bool onMouseDrag(Vec2 local, int button) override;
bool onMouseScroll(Vec2 local, Vec2 scroll) override;
void onMouseEnter() override;
void onMouseLeave() override;
bool onKeyPress(int key) override;
bool onKeyRelease(int key) override;
```

Each mouse handler also has a **rich overload** carrying screen-space
position, movement delta and modifier keys — override either form (the rich
default forwards to the simple one):

```cpp
bool onMousePress (const MouseEventArgs& e) override;     // .pos .globalPos .button .shift/.ctrl/.alt/.super
bool onMouseMove  (const MouseMoveEventArgs& e) override; // + .delta / .globalDelta
bool onMouseDrag  (const MouseDragEventArgs& e) override; // + .delta + .button
bool onMouseScroll(const ScrollEventArgs& e) override;    // .scroll
```

One struct per event kind (no meaningless fields); `button` is an `int`
(compare with `MOUSE_BUTTON_*`). The legacy scalar mirrors
(`x`/`y`/`deltaX`/`deltaY`/`scrollX`/`scrollY`) remain for source
compatibility and are slated for removal at v1.0.

### C. Timer System

Safe delayed execution on main thread, bound to Node lifecycle.

```cpp
// One-shot timer
this->callAfter(1.0, []{ cout << "1 second passed" << endl; });

// Repeating timer
this->callEvery(0.5, []{ cout << "Every 0.5 seconds" << endl; });

// Repeating timer that calls back once for every interval that came due
// (at most 5 per update), e.g. a fixed-rate simulation step
this->callEveryCatchUp(0.01, [this]{ stepSimulation(0.01); }, 5);
```

- Timers are countdowns: each update of the node subtracts `getDeltaTime()`, so they follow the loop (including fixed-rate steps), pause while the node is inactive, and are not affected by `resetElapsedTimeCounter()`. With a fixed update rate they count step time: time the loop drops after a stall (beyond the `setMaxUpdateSteps()` cap) is not counted, so the timer fires that much later in wall time
- Only time after the call counts: a timer starts with the next update after the one it was created in (including an `events().update` listener). In every window it is not charged for anything before the call: the earlier part of a long update or `setup()` (a synchronous load), an idle gap or a stall before an event handler, `draw()` or `runOnMainThread` work created it. One created during a fixed-rate step counts whole steps from the next one. In VSYNC and `setFps()` modes a timer fires on the first update that starts at least its delay after the call; with a fixed update rate it counts steps, so when a frame runs several steps (after a stall, or when the update rate is above the display rate: `callAfter(1.0 / 120)` made in the first step of a frame at a fixed 120 Hz on a 60 Hz display fires on the next step of the same frame) it can fire before its delay has passed in wall time.
- A runtime `setFps()` / `setIndependentFps()` that switches the update into a measured mode (VSYNC or `setFps()`) between updates drops the time since the last update, so running timers fire that much later: under a frame in the usual modes, long only after an idle like `EVENT_DRIVEN`. Called inside an update, the time counts from that update's start. Changing only the draw rate drops nothing (switching between synced and independent update counts as an update-mode change even at the same rate); entering a fixed update rate restarts with one step, which can count a little more or less than the time since the last update
- A node moved during an update, before that update reached it, under a parent the update has already traversed misses that update's countdown, so its timers run one delta late. A node that moves itself from its own `update()` has already been counted down and isn't delayed; one moved under a parent traversed later is counted down once
- `callEvery` keeps its phase (next due = previous due + interval). If an update comes more than a whole interval late it fires once, not once per missed interval
- `callEveryCatchUp(interval, callback, maxCatchUp)` keeps the phase too, but calls back once for every interval that came due, at most `maxCatchUp` times per update. `maxCatchUp` has no default: `0` or `-1` (any value `<= 0`) means no limit. Past the limit the remaining due intervals are dropped; cancelling the timer from the callback stops the remaining calls. Without a limit, a long stall in a VSYNC or `setFps()` loop (or an idle stretch in EVENT_DRIVEN mode) makes it fire that many times at once
- Timers auto-destroyed when Node is deleted
- Zero overhead when no timers are active

### D. Rendering

**State Management:**

```cpp
tc::setBlendMode(BlendMode::Alpha);
tc::enableDepthTest();
```

**Scissor Clipping:**

Nodes can clip children to their bounds (axis-aligned only, performance priority).

**sokol_gl Vertex Buffer Auto-Resize:**

sokol_gl uses a fixed-size CPU vertex buffer (default 64k vertices). When draw calls exceed this limit, subsequent draws are **silently dropped**. TrussC automatically detects and recovers from this:

1. After each frame's `sgl_draw()`, `present()` checks `sgl_error()` for `vertices_full` or `commands_full`
2. If overflow is detected, schedules a resize (4x current capacity) for the next frame
3. On the next `beginFrame()`, `internal::resizeSgl()` destroys all sgl pipelines, calls `sgl_shutdown()` → `sgl_setup()` with larger buffers, then recreates all pipelines. sokol_gfx resources (textures, samplers) are unaffected.
4. One frame of rendering is lost during the resize; subsequent frames render correctly

This is fully automatic — no user action required. The mechanism exists because sokol_gl has no resize API and no way to query how many vertices are needed before drawing.

**Deferred Draw Model (record now, replay at frame end):**

2D (sokol_gl) and 3D (PBR meshes, point clouds, custom-shader draws) have to interleave correctly — a rectangle drawn after a mesh must composite *over* it. sokol_gl cannot be mixed into an arbitrary draw order, so TrussC **records** 3D draws instead of issuing them immediately, tags each with a **layer number**, and replays everything in layer order at the end of the frame:

1. Each submit (`PbrPipeline::drawMesh`, a point-mesh draw, a `Shader` draw) packages a fully resolved command — pipeline, bindings, uniforms — appends it to the current window's queue, then bumps `sglLayerNext` and calls `sgl_layer()` so all subsequent 2D lands on a higher layer.
2. `present()` walks layer 0..N and, per layer, draws that layer's 2D and then its shader / PBR / point commands (`flushDeferredShaderDraws`, `tc/gpu/tcShader.h`).
3. Inside an FBO pass the same thing happens into a separate set of queues with their own layer cursor, flushed by `Fbo::end()` (`flushFboDeferredPbr`, `tc/3d/tcMeshPbrPipeline.h`).

The consequence — and the source of most bugs in this area — is that **record time is not execute time**. Four invariants keep that safe. Anything added to this path must satisfy all four:

1. **Commands capture their inputs by value.** A recorded command holds copies of its bindings and uniform blocks, never references to mutable state. Mutating a material, transform or light after the draw call cannot retroactively alter an already recorded draw.
2. **GPU resources are never destroyed mid-frame.** Owners hand their handles to `internal::deferGpuDestroy()` (`tc/gpu/tcGpuDestroyQueue.h`); they are considered for reclamation after `sg_commit()`. Unconsumed target recordings pin their typed handles and dependency closure; unrelated handles can retire immediately. This covers plain destruction and reallocation alike — when a buffer grows, the old handle stays alive until the commands referencing it have been submitted. Destroying a handle immediately leaves dead handles inside recorded commands, and sokol silently drops the offending draw.
3. **A mutable-content resource read more than once per frame needs snapshots.** An `Fbo` keeps a *version pool*: re-`begin()`ing after the FBO has already been drawn advances to a new version (blitting forward when existing content must be preserved). So `begin/end/draw` followed by `begin/end/draw` in one frame samples the intermediate content for the first draw and the final content for the second, even though both quads execute at frame end. Contract: an Fbo must be drawn from a **single window** within a frame — the pool's frame key is per-window.
4. **All per-frame record state is per-window.** The three swapchain queues and the FBO-pass queues, both layer cursors, the shader stack, the `beginShape` / `beginLines` / `beginStroke` accumulators and the FBO-pass format selectors live in `WindowContext`, not in process globals. Window ticks are serialized on the main thread, but nothing is shared, so one window cannot observe or clobber another's in-flight draws.

Fonts sit outside this machinery: both the bitmap and TTF paths emit quads straight into the active sokol_gl context, so glyphs layer as ordinary 2D content and need no deferral.

**Window execution and capture submission:** The host parks the outgoing
window's open target pass before switching execution, including nested modal
loops and context restoration. The target stack, scopes and attachment identity
survive; the next draw resumes with LOAD. Device submission remains global.
Metal drawables require final authorization from their owner before presentation;
a capture prefix submits encoded work without authorizing presentation.

Synchronous capture parks/submits the renderer prefix before native staging.
Metal async capture stages in that prefix before later source writes. The host
source-selection/finalization interface currently preserves each backend's
existing source behavior. Completed-frame snapshots remain a separate integration.

Each window owns its shadow maps, allocated the first time that window uses
shadows. Its four R32F array layers and shared depth image use 20 MiB of image
payload at the unchanged 1024 default, plus views, sampler and driver overhead.
Windows without shadows allocate none. Deferred PBR, including PBR in an Fbo,
pins its window's map generation and copied slot matrices. Growth creates a new
generation; final pin release links the generation itself into host retirement
without allocating. The GPU dependency collector destroys its fixed handles
after their recordings are consumed and a device boundary permits retirement.
Shutdown drains retired generations before destroying the device, and exit
reclaims any remaining CPU retirement nodes. Partial creation and allocation
failure use the same path. Shadow creation
checks each resource's Sokol state; a GL-only helper also checks native names
and errors within each creation call, draining earlier user errors immediately
before that call and reporting each code in a separate Warning as pending before
shadow allocation. These earlier errors do not reject the allocation. Reuse and
invalid-size rejection do not consume GL errors. Native view failures are detected.
Before a shadow pass rewrites a sampled slot, the target stack consumes queued
PBR that needs its current contents, including a lazy ancestor above which an
Fbo is active. Replay restores the target's viewport/scissor after opening its
pass, before queued PBR runs. Unrelated Fbo production preserves lazy parent
recordings.
Open shadow passes can park across foreign ticks.

ScreenRecorder binds the originating window or Fbo and opens at its source
extent. A take keeps fixed encoder dimensions; mismatched frames are counted and
skipped before copying, with one layout warning per take. Matching frames resume
in the same take. One native capture/encode operation may be outstanding through
staging cleanup; diagnostics are delivered after unlocking within the recorder's
Delivery barrier.
Busy opportunities are counted and skipped with wall/audio PTS gaps preserved.
A replacement start is refused while the previous operation remains pending.
MCP captures use #878's existing admission policy, screenshot credits and request
queue. Each backend reconciles its selected source extent through #891's existing
WorkBudget high-water reservation before allocation, charging growth once.
Two-stage workers destroy captured image payloads before releasing working-memory
credit, including on exceptions. Native jobs retain the same budget and optional
save credit until cleanup, even after a synchronous timeout; an abandoned late
consumer skips output allocation.

**Node Style Isolation:**

Each Node's `draw()` and `endDraw()` start from a clean default style — `resetStyle()` is called automatically before each. This means:

- Every `draw()` begins with white color, fill enabled, no stroke
- Parent style does not cascade to children
- Sibling style does not leak between nodes
- Each Node is fully self-contained: set what you need, draw, done

This design chose full isolation over CSS-like cascading because predictability outweighs the convenience of inheritance. Style leaks between nodes are hard to debug, and explicit `setColor()` calls are cheap.

**Graphics Context Stack:**

```cpp
tc::pushMatrix();
tc::translate(100, 100);
tc::rotate(TAU / 8.);
// ... draw ...
tc::popMatrix();

tc::pushStyle();
tc::setColor(1.f, 0, 0);
// ... draw ...
tc::popStyle();
```

**RAII Scoped Objects:**

```cpp
{
    ScopedMatrix m;
    tc::translate(100, 100);
    // auto pop on scope exit
}
```

### E. Threading

**The Node tree and all GPU/draw state are owned by the main thread.** `setup()`,
`update()`, `draw()`, and input callbacks all run there. Touching the tree
(`addChild` / `removeChild` / `setPosition` / `setColor` / `addMod` / …) or GPU
resources (`Texture`, `Fbo`, drawing) from another thread is a **data race** — it
corrupts `children_` and crashes (not "usually fine": a worker churning the tree
against the per-frame traversal segfaults within seconds).

Several things legitimately run **off** the main thread, and it is easy to reach
for the tree from inside them:

| Runs on a worker thread | Where |
| --- | --- |
| `TcpClient` / `UdpSocket` / `TcpServer` `onReceive` | fired directly on the receive thread |
| `Node::callAfterAsync` / `callEveryAsync` callbacks | the async scheduler thread |
| `AudioEngine` `audioOut` / `audioIn` (incl. `App::audioOut`) | the audio device thread |
| your own `tc::Thread` subclasses | their own thread |

(Console input and MCP requests also arrive on worker threads, but the framework
already marshals those to the main thread for you.)

#### The safe path: `runOnMainThread`

From any thread, hand the work to the main thread instead of doing it inline. It
runs at the start of the next frame, when no traversal is in flight (it runs
immediately if you are already on the main thread):

```cpp
tcp.onReceive.listen([&](TcpMessageArgs& msg) {     // <-- worker thread!
    auto pos = parsePos(msg);                       // ok: local work
    runOnMainThread([this, pos] {
        scene->addChild(makeEnemy(pos));            // ok: runs on the main thread
    });
});
```

In **debug builds**, the structural Node mutators assert they are on the main
thread (`TC_ASSERT_MAIN_THREAD`) so this mistake is caught at its source; in
release the assert compiles to nothing. The assert is a development tripwire, not
a release safety net — the actual fix is `runOnMainThread`.

#### Typed convenience: `Event<T>` `Deliver::Main`

A listener can declare that it must run on the main thread, so you don't write
the `runOnMainThread` wrapper yourself. When `notify()` is fired from a worker
thread, the payload is copied and the listener is marshalled:

```cpp
tcp.onReceive.listen([this](TcpMessageArgs& msg) {
    scene->addChild(makeEnemy(parsePos(msg)));      // delivered on the main thread
}, Deliver::Main);
```

Default delivery is `Deliver::Inline` (runs on whichever thread called `notify()`
— the existing hot-path behaviour). Notes for `Deliver::Main`: the payload `T`
must be copy-constructible (it is captured for next-frame delivery), and the
marshalled copy cannot write back to `arg` or participate in `consumed`
propagation — fine for reactive events like `onReceive` (input events that use
`consumed` already fire on the main thread, where `Main` is a no-op anyway).

#### The exception: `destroy()`

`Node::destroy()` is safe to call from **any** thread. It only flips an atomic
flag; the actual unlink from `children_` is deferred to the main thread's
`sweepDeadChildren()`. So a worker can mark a node for removal directly — it just
can't add/move/reparent one.

#### Primitives

- `std::mutex` + `std::lock_guard` for your own shared state.
- `tc::Thread` wraps `std::thread` with lifecycle management.
- `tc::ThreadChannel<T>` is a thread-safe FIFO (the queue `runOnMainThread` is
  built on); use it directly for other producer/consumer hand-offs.

### F. Console & AI Automation (MCP)

TrussC natively supports **Model Context Protocol (MCP)**, enabling seamless integration with AI agents.

- **MCP Server:** Built-in JSON-RPC server over HTTP only. Stdin commands use the separate console API.
- **Tools & Resources:** Apps can expose functions and state to AI using `mcp::tool` and `mcp::resource`.
- **Standard Tools:** Mouse/Keyboard simulation and Screenshot capabilities are available out-of-the-box.

For development, use `trusscli run --mcp`. Production apps grant inspection
with `mcp::setToolLevel(mcp::ToolLevel::ReadOnly)` in `setup()` (or `Control`
for input). In default builds `TRUSSC_MCP=1` or `TRUSSC_MCP_PORT` opens
only the loopback health endpoint. Registration alone never starts a listener.

See [AI_AUTOMATION.md](AI_AUTOMATION.md) for full reference.

### G. One Instance per Process (header-inline state)

A hot reload build runs the core in the **host** executable and the app (plus its addons) in a **guest** shared library ([BUILD_SYSTEM.md §7](BUILD_SYSTEM.md#7-hot-reload-development)). The guest calls TrussC functions that live in the host, but it also compiles every **header-inline** function and variable it uses into itself. Whether those copies are merged with the host's depends on the platform:

| Platform | How the guest reaches the host | Header-inline state used by the guest |
|---|---|---|
| Linux | unresolved symbols, bound at `dlopen` (host built with `-rdynamic` + `--whole-archive`) | if the host contains the definition too, the host's wins by symbol interposition: **one instance**. If only guest code uses it, the guest keeps its own: **a fresh instance per reloaded generation** |
| macOS | `-undefined dynamic_lookup` (host built with `-export_dynamic`) | if the host contains the definition too, dyld coalesces the guest's weak definition with it: **one instance**. If only guest code uses it, the guest can keep its own |
| Windows | an import library generated from TrussC.lib's exports (a generated `.def`), so the guest imports only TrussC core symbols; app code shared between host and guest goes in the guest or in an addon | only **non-inline** functions are imported; an inline function is compiled into the DLL with its own `static` locals, and an `inline` variable gets its own storage: **a second instance**, and a new one per reloaded generation |

The host contains an inline definition when host code uses it: the core loop in `TrussC.h` (instantiated by the host's `main.cpp`) and the `.cpp` files of TrussC.lib. Everything the core loop reads is therefore shared on Linux and macOS, but state that only app code touches is not: the FBO context, font atlas and IBL bake caches, which only app code fills, were a guest's own there too until #249 moved them out of line.

So on Windows, guest code that registers an MCP tool, calls `setBeepVolume()` or queues work with `runOnMainThread()` through header-inline state writes into its own copy, which the host's frame loop never reads. Nothing fails to compile or link; the feature silently does nothing (#249).

**The rule:**

- State that must be one per process (a singleton, a registry, a queue, a flag that both the core loop and app code touch) is defined **non-inline in a `.cpp`** of the core library, behind an accessor function declared in the header, as in `tcGlobal.cpp` and `tcMCP.cpp`:

  ```cpp
  // header
  namespace internal { Registry& registry(); }
  // tcGlobal.cpp (or a sibling .cpp)
  namespace internal { Registry& registry() { static Registry r; return r; } }
  ```

  Expose it through a **function**, not an `extern` variable: a DLL reads another module's variable only through `__declspec(dllimport)`, which TrussC's headers do not use.
- A `static` local in a header-inline function, or an `inline` variable, is allowed only when a per-module copy is harmless: a cache of derived data, immutable data, or state that only the core loop touches. Say so in a comment at the declaration, and list it in `tools/header_state_allowlist.txt` under one of its four categories (`harmless`, `immutable`, `host-only`, `no-hot-reload`) with the same reason. A cache of GPU objects that nothing frees (sokol_gl contexts, shaders, pipelines, samplers, font atlases) is not harmless: every reloaded generation builds and keeps a new set in the host's sokol pools (with sokol_gl's 4 context slots at the time, FBO drawing stopped after a few reloads).
- Stateless inline code (math, getters, helpers that go through the accessors above) is unaffected and stays inline for speed. Moving state out of line costs one out-of-line call, which matters only on hot paths.

**Added a `static` or `inline` variable to a core header?** Decide in this order:

1. **A constant?** Make it `constexpr` (or a `const` at namespace scope). The check does not flag it; you are done.
2. **A warn-once flag?** Make it a `static OnceGate` (`tc/utils/tcOnceGate.h`) and gate the log line with `isFirstTime()`. A per-module copy only means at most one more line after a hot reload, so the check accepts it by its type; you are done.
3. **Otherwise, unsure?** Move it to a `.cpp` behind an accessor function, as above. This is always correct.
4. **Only a cache of derived data, and you are sure** a separate copy breaks nothing? Keep it in the header and add it to `tools/header_state_allowlist.txt` with its category and reason. The check's failure message prints the line to paste.

`tools/check_header_state.py` enforces this in CI: it scans `core/include`, every `#if` branch included, for `static` locals in functions, `inline` variables, class template static data members and variable templates defined in headers, mutable namespace-scope `static` / anonymous-namespace variables (every declarator of a declaration), and statics in `#define` bodies, and fails on any that is not in `tools/header_state_allowlist.txt` (a function-local `static OnceGate` is accepted by its type and needs no entry). It also fails on an allowlist entry whose category is not one of the four above, and on an entry that no longer matches anything. Its failure message is written for someone new to hot reload: what goes wrong in one sentence, the `.cpp` fix, then a ready-to-paste allowlist line for each finding and when each category applies. The scan is textual: before each run it checks itself against `tools/header_state_selftest.h`, which pins the constructs that once hid state from it (compound-assignment operators, braced default arguments and mem-initializers, ...), but unusual code can still get past it, so review stays the last gate. `constexpr` variables and `const` namespace-scope statics are not flagged: a constant with internal linkage is one copy per translation unit on every platform already. Addons are not scanned: their code lives only in the guest and is recreated on each reload by design.

`core/tests/hotReloadLifecycle` checks the result at run time on every desktop platform: the guest's MCP tools, status entries, control tools and allowed browser origins must reach the host's registry and HTTP server, answer from the current guest generation, and disappear on reload (a deferred reply still pending then is answered with an error if its producer runs guest code, instead of being produced on the deleted App; a host tool's deferred reply survives); settings guest code writes (`setFps()`, `redraw()`, `setTouchAsMouse()`, the clip / fov defaults, `setDataPathRoot()`, a registered glyph, the overlay queries) must reach the host; guest code must see what the host sets (pixelPerfect, the sokol_gl budget, the bitmap-font sampler, the window context being ticked); node ids, and the timer ids guest code's `callAfter()` hands out, must keep counting across generations; the singletons and GPU caches guest code reaches (the AudioEngine, the screen recorder, the async scheduler and its owner numbering, the beep manager, the console state, the PBR and point pipelines, the FBO, IBL-bake and font caches, the node / texture / FBO debug counters) must be the host's instances; `setBeepVolume()` and `mcp::alert()` from guest code must reach the host; work a guest worker thread queues with `runOnMainThread()` must run when the host drains the main-thread queue; and when the host releases an App that guest code attached to a secondary window with the internal constructor and attachment endpoints, guest code must see retirement before its library unloads. Each public `Window::setApp<T>()` constructs a fresh App; existing instances cannot be moved between Windows. The test's host code uses every definition it checks, so on Linux and macOS these pass either way; the Windows run is the one that catches a split. Not everything is checked at run time: `Thread::getMainThreadId()` only indirectly (through `runOnMainThread()` from a guest worker thread), and `deferGpuDestroy()` not at all; for those the static check is the guard.

To reproduce a Windows split on Linux, link the guest with `-Wl,-Bsymbolic` (for example, configure with `LDFLAGS=-Wl,-Bsymbolic`): the guest then binds its own copy of every header-inline definition, as a DLL does, and still reaches everything non-inline in the host. `hotReloadLifecycle` built that way fails on any of the shared state it checks that is header-inline, and passes on the accessors.

---

## 6. 3D Graphics

### Metal Clip Space

Metal uses Z range [0, 1] vs OpenGL's [-1, 1]. Use perspective projection for 3D:

```cpp
tc::enable3DPerspective(fovY, nearZ, farZ);
// ... 3D drawing ...
tc::disable3D();
```

### Lighting System

Two lighting modes are available:

- **CpuPhong** — Legacy CPU-based Phong shading via sokol_gl. Simple but limited.
- **GpuPbr** — GPU-based Cook-Torrance PBR with metallic-roughness workflow.

#### GPU PBR (recommended)

```cpp
void tcApp::setup() {
    light.setSpot(Vec3(0, 200, 300), Vec3(0, 0, -1), 0.0f, 0.45f);
    light.setDiffuse(1.0f, 1.0f, 1.0f);
    light.setIntensity(5.0f);
    light.enableShadow(1024);

    mat = Material::gold();
    env.loadProcedural();
    setEnvironment(env);
}

void tcApp::draw() {
    cam.begin();
    clearLights();
    addLight(light);
    setCameraPosition(cam.getPosition());

    // Shadow pass
    beginShadowPass(light);
    shadowDraw(mesh);
    endShadowPass();

    // PBR pass
    setMaterial(mat);
    mesh.draw();

    clearMaterial();
    cam.end();
}
```

**Light Types:** Directional, Point, Spot (with cone falloff), Projector (texture + lens shift)

**PBR Material Presets:** `Material::gold()`, `Material::silver()`, `Material::copper()`, `Material::iron()`, `Material::plastic(color)`, `Material::rubber(color)`

**Features:** IES photometric profiles, IBL environment maps (HDR/procedural), normal maps, PBR texture maps (glTF 2.0), shadow mapping with PCF (up to 4 shadow-casting lights per frame — run one `beginShadowPass()`/`endShadowPass()` cycle per light; each renders into its own layer of that window's shadow map array)

> **Platform note — IBL on iOS/iPadOS Safari (wasm):** The IBL bake renders into cube-face render targets, which iOS/iPadOS Safari (both WebGPU and WebGL2) cannot do without breaking the canvas swapchain. So `loadProcedural()` / `loadFromHDR()` are **auto-skipped on iOS Safari**: PBR meshes fall back to a flat hemisphere ambient + direct lights (no environment reflections). Direct lighting, normal maps, shadows and plain FBOs all work fine. Native, desktop web, and Android web bake IBL normally. For a consistent look across iOS and other targets, don't rely on IBL reflections. See `tc/3d/tcEnvironment.h`.

---

## 7. Addons

TrussC can be extended with addons (similar to openFrameworks' ofxAddon).

- **Core modules** (`tc::`): Built into libTrussC, always available
- **Addons** (`tcx::`): Optional, require `addons.make` configuration

See [ADDONS.md](ADDONS.md) for details on using and creating addons.

---

## 8. Class Design Policy

### Infrastructure Objects

Policy: Use virtual, encourage inheritance/extension.

- **Examples:** `tc::Node`, `tc::App`, `tc::Window`, `tc::VideoPlayer`
- **Memory:** `std::shared_ptr` automatic management

### Data Objects

Policy: Prioritize performance and memory layout (POD-ness).

- **Examples:** `tc::Vec3`, `tc::Color`, `tc::Matrix`, `tc::Mesh`
- **Memory:** Value semantics, no virtual methods

---

## 9. Native Wrapper Strategy

To avoid GPL contamination and size bloat, these features wrap OS-specific APIs:

### Video & Camera (No FFmpeg)

- **macOS:** AVFoundation + CVMetalTextureCache (zero-copy)
- **Windows:** Media Foundation + Direct3D 11 texture

### Serial Communication (No Boost)

- **macOS/Linux:** POSIX (`open()`, `tcsetattr()`, `read()`); rates without a termios B-constant via `IOSSIOSPEED` (macOS) / `termios2` (Linux), device loss via `poll()` hangup and `EIO` / `ENXIO` / `ENODEV`
- **Windows:** Win32 (`CreateFile()`, `SetCommState()`, `ReadFile()`)

Window handles and App lifetimes
-------------------------------

Runtime strongly owns every open Window. Releasing the last external shared handle does not close it. Explicit close and normal Runtime shutdown detach the App before disposing the native surface and GPU backing. A retained closed handle remains closed across later runs and owns no live backing. There is no default or main Window.

Construct content with `window->setApp<Scene>(args...)`. The internal factory encloses the entire derived constructor in its own owner scope. Direct App construction and attaching an existing shared App are rejected. Factory admission checks the App base against its allocated storage. An App must not contain another App as a member or base other than its own App base; setApp<T> fails with an Error if it does. The range check can admit an incidental App subobject inside that storage, allowing its constructor body to run before the outer construction fails. Factories use allocator storage and placement construction with matching destruction/deallocation; class-specific allocation overrides are bypassed. A direct App outside the allocated storage, or after the ticket is claimed, throws `App construction requires Window::setApp<T>() or a launcher` before its derived body; an uncaught exception also fails the enclosing factory construction. Constructor failure and superseded pending requests retire only that candidate, without inventing setup, exit or cleanup. `destroyApp()` leaves the Window empty. Attachments retain the same event slots, settings and clock across replacements.

Close, replacement, removal, headless shutdown and guest destruction use the same App ending: detach Window/root, deliver exit once, disconnect audio and wait, clean up the tree (only the App virtual in headless mode), then retire subscriptions, tools, completions and weak references. Normal Runtime shutdown retains one snapshot in reverse attachment order. Every Window backing is disposed before the graphics device and services stop.

Runtime also owns OS session query/commit. Win32 uses a hidden top-level control HWND that outlives user windows; macOS forwards applicationShouldTerminate and NSWorkspaceWillPowerOff on the main queue. Busy means host/App code on the main stack, including native startup, constructor, callback and tick probes; workers alone do not count. Idle queries reuse Runtime-first exitRequested; busy queries accept without entering App callbacks. Commit is unvetoable and deduplicated. Busy commit invokes no App exit or cleanup and arms no watchdog. Windows returns from WM_ENDSESSION and lets the OS end the process; macOS calls _exit(0) before NSTerminateNow. Idle commit runs normal reverse ending and complete cleanup synchronously, then ends the process without returning to frames. There is no session freeze or interrupted-constructor ending.

The OS-only watchdog bounds one idle ending across all Apps. Its worker/storage are prepared at run admission and armed only for an idle commit. Runtime::setLogoutTimeout(float seconds) defaults to 3 seconds per run: negative/NaN log Error and keep the old value, zero ends immediately, and positive infinity disables the deadline. Ordinary Cmd+Q/exitApp/last-window close never arms it. Windows may enforce an earlier deadline; no fixed macOS logout allowance is asserted. App::exit should save briefly. On accepted ordinary macOS termination, Runtime retires every Window before returning NSTerminateNow, so AppKit cannot later release GPU targets after sg/sgl shutdown. Linux hosts without a session callback and mobile/Web host-stop events carry no desktop logout promise.

A quit requested in setup reaches exitRequested before audio subscription and before the first service prelude. Acceptance permits no further App callbacks; veto continues normal operation.

Argument-free Window APIs in App callbacks, owned listeners and async completions resolve the App token. Unowned code must have exactly one open Window to omit its target; zero or multiple Windows produce an error. MCP uses positive Window IDs and returns the available IDs for an ambiguous or invalid target. Every development Window title carries [MCP].

`quitOnClose` defaults to false for every Window. `setQuitOnClose(true)` requests normal application quit, allowing veto. An OS close of the last open Window or a quit-on-close Window queries process quit before closing anything. A veto keeps all Windows open. Code close skips closeRequested; last-window/quit-on-close asks exitRequested before any closure, and a veto keeps all Windows open. A non-last plain code close asks no events. App::exit also runs on replacement, destroyApp and hot reload, so it is unsuitable for expressing a window-close policy. A Window exit listener may request process quit after closure, but cannot provide the before-close veto guarantee of setQuitOnClose. Calling exitApp during Runtime shutdown does nothing.

App code uses `events()`; non-App code (addons, app-wide handling) uses `runtime().events()`. Runtime owns exitRequested, deviceLost, hotReloadUnload and console. Lifecycle events notify Runtime first, then one reverse-attachment App snapshot (reload targets replacing Apps only). Quit gives every listener fresh cancel=false, ORs all vetoes, keeps the first nonempty veto reason and treats a throw as a veto while continuing. Device loss keeps its mutable cancellation behavior. Console is Runtime-only, once per subscribed slot, with subscriber ownership restored. See MIGRATION_0_8.md for console::start and the removal of CoreEvents::console.

OS close asks the addressed Window's closeRequested with the same per-listener OR/throw-veto policy. On acceptance, quitOnClose/last-window process quit asks exitRequested before any closure. Either veto keeps all Windows open; both accepted use common ending. Window::close and process shutdown bypass closeRequested. App swap/destroy/reload call App::exit without a Window exit. exitApp inside App::exit or Runtime shutdown is inert; an owned Window exit listener can request normal quit while the process is still running. ImGui deadlines run in Runtime maintenance before MCP expiry, including no-draw turns.

Runtime queue and maintenance work selects process context and progresses at safe outer host turns, including turns with no Window draws. Swapchain drawing requires a Window callback; explicit offscreen Fbo drawing remains available. Nested entries restore the caller's drawing and native state. Native creation establishes identity, resources, dimensions and binding readiness before App construction. Each resized frame reconciles its Window framebuffer, swapchain and automatic viewport before beginning a pass.

Multiple Windows are supported on macOS, Windows and Linux X11. Create a Window with `createWindow(settings)`, then request content with `window->setApp<Scene>(args...)`. Unsupported platforms return nullptr with a diagnostic. Each Window owns its App, event stream, clock, input and drawing state. GPU resources such as Texture, Fbo, Mesh and Font can be shared across Windows. The Runtime retains every open Window until explicit close or shutdown; closing the first-created Window leaves the others running. `App::getWindow()` returns its attachment while it is live, and nullptr during ending or in a headless App.

Every Window keeps stable App event slots installed before construction. BeforeApp listeners default to priority 0, the App slot uses 100, and AfterApp uses 200. The App virtual runs before its eligible scene tree. Later priority-100 listeners follow the stable App slot in registration order. Consumed input skips later App, tree and listener delivery. `clear()` removes ordinary listeners and preserves the framework slots; replacing or removing an App leaves those slots on the Window. The hot-reload host and MCP input use the same delivery path. Headless executes only the App virtual update and ending, with no tree drawing.

App audio callbacks attach after the first setup completes. Ending detaches the Window/root, delivers exit once, disconnects this App's audio callbacks and waits for callbacks already running, then calls cleanup and retires its owned work. This order applies to close, replacement with `setApp<Scene>()`, removal with `destroyApp()`, shutdown, headless and guest destruction. A retired App cannot be attached again; construct a fresh instance through the destination Window. Audio callbacks must not wait for the main thread or a lock it needs during ending.

Guest retirement cancels registered callbacks and releases their captures. Queued main-thread copies check owner liveness at execution and retain captures until the queue destroys them. Every successfully loaded module remains mapped for process lifetime, so App, Event, resource and queue objects use ordinary shared storage without per-object code references or symbol lookup. Maintenance does not physically unmap retired modules.

Hot reload supports a single window. While a hot-reload guest is loaded, creating a second or later open window emits a Warning each time; behaviour is not guaranteed. Reload happens at the main-thread outer-turn safe point and replaces the launch Window App if that Window is still open. A request sets a flag; the next poll clears it and reloads once. Requests made during an attempt remain for the next poll. Successful modules stay loaded for the process lifetime. iOS/Android/Web use the static fallback.
