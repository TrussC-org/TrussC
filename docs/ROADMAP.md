# TrussC Roadmap

---

## Current Status

**Platforms:** macOS, Windows, Linux, Web (Emscripten), Raspberry Pi, iOS, Android


## Planned Features

### High Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| Multi-light shadows | Support shadow maps for multiple lights simultaneously | High |
| Area lights | Rectangle / disc / line area light sources | High |
| Cascaded shadow maps | CSM for directional lights (large outdoor scenes) | High |
| Unified `LoadResult` API | Replace `bool` return of `Image::load` / `SoundBuffer::load` / `Video::load` / `Font::load` / `Shader::load` with a shared `trussc::LoadResult` carrying `LoadError` enum + message + raw code. Use `explicit operator bool()` so existing `if (x.load(...))` keeps working. Needs an audit of error taxonomy first (file-not-found / invalid-format / permission-denied / decoder-failure / etc.) and a per-domain inventory of what error sources exist (stb_image, AVFoundation, mbedTLS, miniaudio, etc.). | High |
| Audio device hot-plug handling | Detect device disconnect (USB DAC unplugged etc.) via miniaudio's `ma_device_notification_proc`. On detection, auto-fail-over to the system default device, then fire `AudioEngine::audioDeviceChanged` with the new device's info (today that event only fires from `init()` — initial + user re-init — not from unplug). Listeners can override the fallback by calling `init(settings)` themselves. Without this, calling `init(settings)` to switch devices after a hot-unplug will hang in `ma_device_uninit` waiting for the dead device's audio thread to join. Likely also wants a `cause` enum on `AudioDeviceChangedArgs` (InitialInit / UserRequest / DeviceDisconnect) so listeners can distinguish event sources. | Medium |
| Custom-shader textured quad path (sgl-integrated) | A TrussC-level drawing path that issues `sg_apply_pipeline` + `sg_apply_bindings` + `sg_draw` with a user-supplied shader, while staying ordered with respect to surrounding `sokol_gl` commands. Built on the existing `suspendSwapchainPass()` / `resumeSwapchainPass()` machinery (already used by FBO), plus a partial-flush of the default sgl context so a `drawRect → font/customShader → drawCircle` sequence renders in submission order. Unlocks: R8 / RG8 / non-RGBA texture formats (current sgl shader forces RGBA8 with `(255,255,255,A)` padding), font atlas R8 storage (see below), sensor-data visualization (depth/IR/grayscale with palette mapping), SDF text, mask compositing, future Path glyph rendering. Probably surfaces as `Texture::drawWith(Shader&, ...)` or similar on the TrussC `gpu/` side; sgl_gl_tc fork ideally stays untouched. | High |
| Font atlas R8 storage | Drop font atlas from RGBA8 to R8 (4× memory reduction — e.g. ~4 MB → ~1 MB on a large CJK atlas). Currently blocked on the custom-shader path above: the sgl built-in shader returns `(R, 0, 0, 1)` from an R8 sample, which renders as solid red instead of `(color.rgb, glyph.a)`. Once that path lands, swap the atlas pixel format + add a tiny font-specific fragment shader doing `out = vec4(uColor.rgb, sample.r * uColor.a)`. No quality loss (glyphs were already grayscale alpha stored as `(255,255,255,A)`). Sokol R8 is native on all current backends (Metal / D3D11 / GL 3.3 / GLES3 / WebGL2 / WebGPU — GLES2 / WebGL1 already dropped). Pair with this PR cycle's tategaki work to make Japanese / CJK use cases cheap on RAM. | Low (once shader path exists) |
| Mouse event bubbling (press/release/drag) — **v0.7.0, breaking** | Unify input propagation with the scroll path, which already bubbles ("return false to allow bubbling to parent", see `RectNode::onMouseScroll`). Today `dispatchMousePress` delivers to the front-most hit node ONLY — if its `fireMousePress` returns false the event silently dies (no ancestor walk, no occluded-sibling fallback), so press/release/drag and scroll follow different rules. Plan: when the hit node does not consume, walk the parent chain (localizing via `localizeMouse` per level, firing node + mod hooks), and the first consumer becomes `grabbedNode` for drag tracking. Keep `RectNode::onMousePress`'s `return true` (consume) default, so bubbling activates only where a handler explicitly returns false — a path that today just swallows the event, making the change near-compatible in practice but still semantically breaking (hence the 0.7.0 window). Kills the current anti-pattern where a child must know its parent to forward input (contradicts the "dependencies point downward only" rule) and unblocks drag-through-children cases (grab a window by its title bar across label children; drag-scroll a list over its buttons). Watch mouse enter/leave consistency along the chain. **Considered and deferred, revisit after bubbling lands:** flipping hit-testing to default-ON ("visible RectNode catches clicks", DOM/Unity-style, opt-out via `disableEvents()`). Only sane as a package with bubbling + consume-by-choice (make `RectNode::onMousePress` return whether a listener consumed instead of unconditional true); default-ON alone under single-target dispatch would make every decorative label/separator child swallow its parent widget's clicks. Perf is a non-issue either way: the hit-test walk already visits all active+visible nodes (Mat4 inverse + children snapshot per node) and `eventsEnabled_` only gates the final rect test. | Medium |
| Coroutine sequencing (`wait` / `tween` / `event` awaiters) | Write time-spanning procedures as procedures: `move(); wait(1); move();` instead of callback chains or hand-rolled state machines in `update()`. Two layers, cheapest first. **Lua (TrussSketch)**: Lua coroutines are native — the host only needs a scheduler that resumes yielded coroutines next frame / after N seconds; `wait(1)` = sugar over `coroutine.yield(1)`. Scratch-style sequencing lands in sketches within days of work. **C++20**: TrussC already requires C++20, so `co_await` is available — a `Task` type + frame-loop-driven awaiters (`co_await wait(1.0)`, `co_await tween(...)`, `co_await event(node.mousePressed)` = "suspend until clicked"). Big win for installation flow control (idle → attract → interact → reset). Ship Lua first, stabilize the C++ API shape against real sketch usage. Purely additive — no existing path changes. | Medium (Lua) / High (C++) |
| ScreenRecorder audio track | Record audio (app output and/or mic) into the video file alongside the frames. **The one item in this batch with real regression surface**: it modifies the working per-platform encode pipelines (AVFoundation / Media Foundation / ...) to mux an audio track with correct A/V sync — so it should ship as its own release, verified on real hardware per platform, not bundled with other work. Pairs naturally with the real-time audio events (`audioOut` already exposes the exact buffers to record). | Medium-High |
| Audio analysis: onset / beat / Mel bands | FFT already exists (`tcFFT.h`); layer the standard music-visualization trio on top: **onset detection** (spectral flux — the moment a drum hit / note attack happens), **beat tracking** (period estimation over the onset train → BPM + phase), **Mel-band energies** (perceptual frequency bands, the usual input for reactive visuals). Makes "visuals that react tightly to music" a one-liner — a workshop staple that currently requires hand-rolling. Additive on the existing FFT chain. | Medium |
| GPU compute (`tc::ComputeShader`) | The vendored sokol_gfx already supports compute passes (`sg_begin_pass({.compute=true})` + storage buffers/images + dispatch; sokol-shdc cross-compiles `@cs` shaders). Wrap it TrussC-style and ship a flagship GPU-particle example (1M particles). **Backend caveat**: compute works on Metal (macOS/iOS), D3D11 (Windows), GL/GLES3.1+ (Linux/Android), and **WebGPU only on web — WebGL2 is excluded**, so compute examples can't run in the current WebGL2-based web player (mark `webSupported: false`, or revisit when the player moves to WebGPU). Desktop/mobile are fully ready today. | Medium-High |
| Input record & replay (`tc::InputRecorder`) | Record timestamped input events (mouse / key / MIDI / OSC) to a file and replay them deterministically through the same event bus MCP injection already uses. Three uses for the price of one: **attract mode** for unattended exhibits (replay a canned interaction), **repro debugging** (capture the "happens sometimes" and take it home), **regression tests** (record an interaction once, assert on the result in CI or via MCP). Additive — replay enters through the existing injection path. | Medium |
| Frame vector export (SVG / PDF) | `beginVectorCapture("frame.svg")` records the frame's draw calls (rects, circles, paths, text-as-paths) as vectors instead of pixels. Serves the pen-plotter (AxiDraw) and print/generative-poster crowd — a real community that today stays on Processing for its PDF export; oF's ofxCairo path is effectively unmaintained. TrussC's thin unified draw API makes the interception layer tractable. Opt-in capture: zero cost when off (verify that). Raster-only features (shaders, textures) are out of scope or rasterized-on-import. | Medium-High |
| Kiosk / installation mode (trusscli) | First-class answer to "museum runs an app for 3 months": `trusscli kiosk` supervises the app as a child process — watchdog auto-restart on crash/hang (via local `/health` heartbeat), log rotation, crash-report collection (.ips etc. + log tail attached to the restart notification), scheduled start/stop. **App side stays zero-config**: kiosk passes the log path (`Logger` already has `setLogFile()` + `Event onLog` for extra sinks) and rides the existing MCP HTTP server for `/health`. **Outbound sinks = ONE templated HTTP POST engine, not per-service adapters**: URL + body template + `{{app}}/{{event}}/{{msg}}/{{lines}}` variables; named presets prefill it (Uptime Kuma push, Discord/Slack webhook, VictoriaLogs/Loki ingestion, generic JSON POST). Two modes per sink: event notification (one-shot on error/restart/down) and log stream (batched). The genuinely hard part is the **offline spool**: venue networks drop for hours, so buffer to disk and re-send on recovery — that lives in the engine once, works for every sink. **Periodic thumbnails without the fps hitch**: today's `get_screenshot` does a full-res readback + synchronous PNG encode (tens of ms = a visible hiccup); the monitoring path instead blits the frame into a small FBO (~512px) first, reads back only that, and JPEG-encodes off the main thread — sub-ms on the frame loop, so a 60fps installation never stutters. Full-res PNG stays available on demand for debugging. **MCP tool passthrough**: kiosk relays tool calls to the app's MCP endpoint, so app-specific tools (registered via the existing `tool()` API — e.g. a venue `grab_webcam` snapshot) are reachable remotely with zero new mechanism; kiosk is transport, features stay in the app. Mostly trusscli / process-level — near-zero core regression risk. | Medium |
| Fleet dashboard (`trussc fleet`) | The server counterpart to kiosk, once the kiosk JSON (heartbeat / log / thumbnail) has hardened in real use: a small self-hosted service that receives every installation's heartbeats, logs, and periodic thumbnails. **Architecture: monolithic, store-is-the-hub** (decided over a split broker + store: history is a hard requirement so a store always exists, and a separate broker adds a hop + an ops burden without removing any custom code; MCP passthrough is RPC and fits a persistent connection, not pub/sub). Each kiosk keeps ONE outbound WebSocket to the server (NAT-friendly, auto-registers with app-id + token on connect — no manual IP entry, venue IPs are meaningless behind NAT); the server appends to storage AND fans out to live viewers via in-process pub/sub, and exposes a WS/SSE subscribe endpoint so external consumers (extra viewers, a live AI session) can attach broker-style. MQTT can be added later as an optional ingest transport (server subscribes to a broker) without changing the design. **Storage: DB-free by design** — logs are append-only JSONL files rotated daily (`apps/<id>/logs/<date>.jsonl`), thumbnails are timestamp-named JPEGs (the directory IS the index), registry is a JSON per app. At exhibit scale (a few MB/day/app) server-side file scan answers "ERROR ± 50 lines last night" at ripgrep speed; append-only files are crash-proof, migration-free, and readable with `less` when everything else is broken. SQLite (itself stb-class as a dependency — single amalgamation file, in-process) enters only later and only as an INDEX (FTS5 over long ranges, transactional metadata like incident notes / tokens), with files staying the source of truth. The headline is the **live thumbnail wall** — "what does every venue look like right now" at a glance, which generic monitoring (Uptime Kuma etc.) can't do by construction, and which replaces the physical camera people end up pointing at their own installations. The second headline: **the fleet server is itself an MCP server** (`search_logs`, `tail_logs(follow)`, `get_screenshot_history`, `restart_app`, plus passthrough to each app's own MCP tools) — so an AI assistant can investigate "the Osaka piece crashed last night" end-to-end: search the logs, pull thumbnails around the error, form a hypothesis, even live-debug the running app through the same node-tree/ImGui tools used in local development. **Requires auth + per-venue scope control from day one** (token, read-only vs full-control) — the passthrough surface can mutate app state. Not a Kuma/VictoriaLogs replacement (use those meanwhile); this is vertical TrussC-only value. Candidate for dogfooding as a headless TrussC app (tcxCrow). | Medium-High |
| Spring / smoothDamp utilities | `smoothDamp(current, target, velocity, smoothTime)` (critically damped, Unity-style) + a tiny spring type. "Follow the mouse with nice easing" is hand-rolled by every user today; two functions in tcMath end that. Zero risk, high daily-use value. | Low |

### Medium Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| VBO detail control | Dynamic vertex buffers | Medium |
| Auto-growing per-frame uniform buffer | The Metal/WebGPU/Vulkan uniform ring is FIXED at `sg_setup` (4MB default ≈ 8k draw calls/frame, each apply 256-byte aligned); overflowing it in a release build silently corrupts uniforms (flipped / fully black frames — found via the suzuki-rain high-density bug). `WindowSettings::reserveUniformBuffer` (2026-07) covers known peaks up front, vector::reserve-style. The full fix is growing on overflow: Metal legally allows swapping the bound buffer mid-encoder (`setVertexBuffer` again), so on overflow a freshly allocated larger buffer can take over at offset 0 with ZERO dropped draws (already-recorded bindings keep reading the old buffer, which the command buffer retains) — worst case is one allocation hitch (~ms) on the grow frame, which a sufficient reservation avoids. Requires patching vendored sokol_gfx (`_sg_mtl_apply_uniforms` + commit path to re-create the inflight pair). Note: `sg_frame_stats` proved UNRELIABLE for pre-overflow detection — measured `size_apply_uniforms`/`num_apply_uniforms` did not match the ring's actual offset progression (crash at 6MB while stats estimated ~0.5MB); investigate that gap as part of this work. | Medium |
| macOS deprecated API migration | Replace `tracksWithMediaType:` / `copyCGImageAtTime:` with async equivalents (deprecated in macOS 15.0) | Medium |
| `SG_VERTEXFORMAT_INT10_N2` adoption | sokol_gfx (2026-05) added a 10-10-10-2 normalized int vertex format. Adopt for `tcMesh` normal / tangent attributes — 3x smaller than FLOAT3 with effectively no visual loss (Unity / Unreal default). D3D11 backend not yet supported upstream, so verify Windows path before committing. | Medium |
| `TextureFormat` expansion | `tc::TextureFormat` currently exposes RGBA8/16F/32F, R8/16F/32F, RG8/16F/32F (+ `BGRA8` and `RGBA16`, added 2026-05-31 for tcxSyphon BGRA interop and tcxNozzle 16-bit interop). sokol_gfx offers more that are worth adding *when a concrete consumer appears* — not speculatively, since each needs createResources / FBO-attachment / blend-pipeline (`write_mask`) / `draw()` sampling verification per format. Candidates, roughly by usefulness: **`SRGBA8`** (`SG_PIXELFORMAT_SRGBA8`, correct sRGB color management) · **`RGB10A2`** (`SG_PIXELFORMAT_RGB10A2`, 10-bit HDR / wide gamut — pairs with the 10-bit output row below; likely the next real need) · **`RG11B10F`** (`SG_PIXELFORMAT_RG11B10F`, cheap packed HDR float, no alpha) · **`RGB9E5`** (`SG_PIXELFORMAT_RGB9E5`, shared-exponent HDR, read-only) · minor / niche: signed-normalized variants (`R8SN`/`RG8SN`/`RGBA8SN`), integer formats (`R32UI`/`RGBA32UI` etc. for compute / data textures), `R16`/`RG16`/`RGBA16` unorm-16 (depth-like precision without float), and depth/stencil formats (`DEPTH`, `DEPTH_STENCIL`) if we ever expose them as sampleable. | Low (per format, on demand) |
| Configurable 10-bit color output | TrussC currently forces RGB10A2 swap-chain in sokol_app patches. Make it opt-in via WindowSettings once upstream sokol adds a `SAPP_PIXELFORMAT_RGB10A2` (currently not in upstream — track [floooh/sokol](https://github.com/floooh/sokol)). | Low |
| Dear ImGui 1.92.6 → latest | Currently on 1.92.6 WIP. 1.92.8 (2026-05-12) introduced a notable breaking change: `AddRect()` / `AddPolyline()` / `PathStroke()` swapped their `flags` and `thickness` arg positions. TrussC core does not touch these, but user code that does will need updates. Inline redirection keeps source compatible unless `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` is on. Plan a single bump to the latest 1.92.x. | Low |


---

## Addon Candidates

| Addon | Description |
|:------|:------------|
| tcxTimeline | Keyframe timeline editor for any `TC_REFLECT`ed property — the building blocks all exist in TrussC already (property enumeration via reflection, tcxImGui for the editor UI, JSON serialization for saving tracks). Spiritual successor to ofxTimeline / Duration (James George, developed with YCAM InterLab), which is effectively unmaintained — its users (installation / show-control / VJ work) have nowhere current to go. Also the natural showcase for the reflection system. |

---

## oF Compatibility Gap

Features available in openFrameworks but missing in TrussC.

| Category | Method | Description | Priority |
|:---------|:-------|:------------|:---------|
| System | `launchBrowser` | Open URL in default browser | Medium |

---

## Platform-Specific Audio Features

### AAC Decoding (`SoundBuffer::loadAacFromMemory`)

| Platform | Status | Implementation |
|----------|--------|----------------|
| **macOS** | ✅ Implemented | AudioToolbox (ExtAudioFile) |
| **Windows** | ⬜ Not yet | Media Foundation (planned) |
| **Linux** | ✅ Implemented | GStreamer |
| **Web** | ✅ Implemented | Web Audio API (decodeAudioData) |

Used by: TcvPlayer, HapPlayer (for AAC audio tracks)

---


## External Library Updates

TrussC depends on several external libraries.
Image processing libraries are particularly prone to vulnerabilities, so **check for latest versions with each release**.

| Library | Purpose | Update Priority | Notes |
|:--------|:--------|:----------------|:------|
| **stb_image** | Image loading | **High** | Many CVEs, always use latest |
| **stb_image_write** | Image writing | **High** | Same as above |
| **stb_truetype** | Font rendering | **High** | Upstream explicitly states "NO SECURITY GUARANTEE — do not use on untrusted font files". See [docs/SECURITY.md](SECURITY.md). |
| **mbedTLS** (tcxTls) | TLS for tcxTls / tcxWebSocket | **High** | Track the v3.6.x LTS branch for CVE fixes. Current: v3.6.2. |
| pugixml | XML parsing | Medium | |
| nlohmann/json | JSON parsing | Medium | |
| sokol | Rendering backend | Medium | **TrussC has customizations (see below)** |
| miniaudio | Audio | Medium | |
| Dear ImGui | GUI (tcxImGui addon) | Low | Use stable versions |

**Update Checklist:**
- Check GitHub Release Notes / Security Advisories
- For stb, check commit history at https://github.com/nothings/stb (no tags)

### sokol Customizations

`sokol_app.h` has TrussC-specific modifications. When updating sokol, these changes must be reapplied.

See: [`core/include/sokol/TRUSSC_MODIFICATIONS.md`](../core/include/sokol/TRUSSC_MODIFICATIONS.md)

---

## Reference Links

- [oF Examples](https://github.com/openframeworks/openFrameworks/tree/master/examples)
- [oF Documentation](https://openframeworks.cc/documentation/)
- [sokol](https://github.com/floooh/sokol)
