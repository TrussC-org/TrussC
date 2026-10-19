# ImGui through a quad warp

An independent ImGui context renders into a 320 × 240 Fbo, enlarged through a
perspective quad warp. Click the button or drag the slider. Press **W** to show
the handles, then drag corners to change the surface; keep the quad convex.
The orange dot shows the pointer mapped into Fbo pixels.

The Fbo resolution sets the UI's sharpness: the 320 × 240 Fbo is magnified
several times on screen (about 4× on a Retina display), so text looks soft. A
larger Fbo, sized for the on-screen quad, gives crisper text but a smaller UI
in Fbo units, so widget sizes need to grow with it.

`tcApp::screenToFbo(screen, fboPx)` uses `QuadWarp::screenToSource()` for the
inverse warp, then checks the Fbo bounds. The helper rejects invalid or folded
quads and non-finite results or non-positive homogeneous coordinates.
Keep the quad convex when editing its handles so rendering and input stay valid.
`pointerInput(screenPos, action)` uses this helper and records the latest
position and button state, with one latch to preserve a short click. `update()`
forwards the position and at most one button transition to offscreen ImGui per
frame. A press and release between frames produces one click over two frames;
additional presses are coalesced. Only `Down` starts a press; `Drag` keeps an
existing press, and `Hover` or `Up` clears the down state. Outside input,
window leave and focus loss cancel the gesture and hide the dot. Cancellation
forwards an invalid-position release for one frame, before any later coalesced
hover or fresh press, so reentry cannot complete a canceled click.
These use four input state members: position-or-invalid, down, pressed latch,
and pending cancellation. Per-frame forwarding keeps ImGui's input queue small.
Call the same function from a TUIO, OSC or touch handler to replace the mouse
(one pointer, mapped to the left button), using `PointerAction::Down`, `Drag`,
`Hover` and `Up` for the corresponding source events. When the cursor disappears,
call `pointerInput({}, PointerAction::Cancel)`, including for an unforwarded
press. The source stays released until a new `Down`.

Build from the repository root using the usual generated project files:

```sh
tools/bin/trusscli update -p addons/tcxImGui/example-quadWarp --tc-root .
cmake -S addons/tcxImGui/example-quadWarp -B addons/tcxImGui/example-quadWarp/build-linux -G "Unix Makefiles"
cmake --build addons/tcxImGui/example-quadWarp/build-linux
addons/tcxImGui/example-quadWarp/bin/example-quadWarp
```

Pass `--self-check` under a display (Xvfb works on Linux) for synthetic input,
mapping, cancellation, per-frame input forwarding, handle toggling and ImGui button clicks.
The checks live separately in `src/selfCheck.cpp`.
