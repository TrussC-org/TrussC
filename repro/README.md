# TrussC deferred-rendering bug repro apps

Three test apps for verifying the deferred-rendering fixes. This branch
(`test/deferred-repro-apps`) sits on top of `fix/shader-deferred-capture`,
which includes `fix/deferred-mesh-buffers-and-texture-blend` (PR #183) and
`fix/generic-deferred-gpu-destroy`. Issues: #184 #187 #188 (fixed here),
#189 #190 #191 (spec'd, intentionally NOT fixed yet).

## Setup (per app)

1. `trusscli new <appName>`, then replace the generated `src/` contents with
   the files from `repro/<appName>/src/`. Keep the generated `main.cpp`
   (the repro apps only replace tcApp.*). shaderRepro needs
   `src/shaders/test.glsl`; fboSemanticsRepro needs `src/effects.glsl` (src root — its tcApp.h includes "effects.glsl.h" without a shaders/ prefix)
   (both included here).
2. Configure against THIS checkout's core:
   `cmake --preset <platform> -DTRUSSC_DIR=<this-checkout>/core`, then build.
3. Run with `TRUSSC_MCP=1 TRUSSC_MCP_PORT=<port>`, screenshot via the MCP tool
   `tc_save_screenshot`, judge visually. (JSON-RPC 2.0 over
   `POST http://localhost:<port>/mcp`; send `initialize` once first.)

## Expected results ON THIS BRANCH

### lifetimeRepro
- R1 (scope-local Image drawn then destroyed): shows MAGENTA (bug = blank)
- R4 (mipmapped dynamic texture, update-after-draw): shows CYAN (bug = blank)
- Atlas growth frames: no draws vanish on growth frames (bug = 1-frame wipeout)
- Frame 2400 glyph overflow burst: one-time "Glyph atlas is full" warning and
  NO crash (bug = SIGBUS)

### shaderRepro (mode A first ~2s, mode B after)
- Test 1: left quad RED, right quad GREEN (bug = both green)
- Test 2: left texquad BLUE (bug = yellow)
- Test 3 (mode B, scope-local Shader every frame): NO crash, cyan quad
  (bug = segfault in sg_apply_pipeline at present())
- Test 4: shader quad appears only inside the small corner fbo copy
  (bug = full-size on screen + missing from the fbo)

### fboSemanticsRepro (ROUND 2 — #189/#190/#191 now FIXED on this branch)
- mode 0 (1 marker rect top-left): left square RED, right square BLUE
  (#189 fbo version pool; bug = both blue)
- mode 1 (2 marker rects): yellow left half GONE — dark gray + cyan rect only
  (#190 clear() erase semantics; bug = yellow visible)
- mode 2 (3 marker rects): fullscreen gradient VISIBLE in BOTH halves; the
  fbo half additionally shows the green corner copy (#191 pass LOAD;
  bug = gradient wiped when the fbo block runs)

Round-2 platform attention: #191 required a small documented patch to the
vendored sokol_gfx.h METAL backend only (MSAA swapchain store action). D3D11
and GL are believed to preserve the MSAA target natively — mode 2 on
Windows/Linux is the key thing to confirm. Also confirm mode 0 MSAA fbo
behavior (the version pool shares the MSAA render target across versions).

## What to report per platform

- Each expected line above: MATCH / DIFFER (+ screenshot)
- Especially interesting on Linux/GL: dangling views may have been tolerated
  by driver refcounting. Please ALSO build the repro apps against
  `origin/main`'s core (pre-fix) and report whether the lifetime bugs
  (lifetimeRepro R1/R4, shaderRepro tests) even reproduce there on GL.
- Any crash: attach the stack/log.
