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
| Real-time audio stream API | Expose the audio callback so user code can synthesize / process samples per buffer (typical 256–512 frames at 96 kHz). oF-style listener pattern: `App` gets an overridable `audioOut(float* out, size_t frames, int channels)` (and `audioIn(const float* in, ...)`); plus a free-function or `SoundStream` class for non-App-bound code. Currently the only way to produce sound is pre-baked `SoundBuffer` + `Sound::play()`, which can't handle dynamic synthesis (sine generators with live parameter changes, granular synthesis, software synths, audio effects, etc.). Output runs at engine native rate (96 kHz, no rateRatio path) — document that. Likely paired with `tc::AudioSettings` (sample rate / buffer size / channel count / max polyphony) passed to a new `AudioEngine::init(settings)` overload, so the engine config is exposed at the same time. | High |
| `AudioEngine::init(AudioSettings)` overload | Currently `AudioEngine::SAMPLE_RATE` / `NUM_CHANNELS` / `MAX_PLAYING_SOUNDS` are `static constexpr`. Wrap them in a `tc::AudioSettings` struct so `AudioEngine::init(settings)` (called at the very start of `setup()` before any `Sound::load()`) can opt into a different sample rate (48 kHz for mobile CPU savings, 192 kHz for hi-res), buffer size, polyphony, etc. Existing zero-config callers keep working — `init()` without args = current defaults. Late callers (after auto-init) get a warning + ignored settings (silent re-init is too disruptive to already-playing sounds). Move the constexpr references in tcVideoPlayer_linux.cpp etc. to a runtime `getSampleRate()` accessor. Often implemented in the same PR as the real-time stream API entry above. | Medium |
| Streaming audio playback | New `Sound::loadStream(path)` (sibling of `Sound::load`) that keeps the underlying `ma_decoder` alive and reads from disk on demand via a worker-thread-fed ring buffer (~16 KB prefetch). Cuts memory cost for long files (multi-minute BGM, podcasts) from "full PCM in RAM" (e.g. ~80 MB for a 4-minute stereo track decoded to float) to a fixed-size prefetch. WAV / MP3 / FLAC ride miniaudio's incremental `ma_decoder_read_pcm_frames`; OGG path mirrors with stb_vorbis incremental reads. AAC stays on-memory (platform decoders buffer internally). Short SFX should keep using on-memory `load()` — streaming is opt-in. Seek requires re-fill of prefetch and ~10 ms blackout, acceptable for music. | Medium |
| Audio device hot-plug handling | Detect device disconnect (USB DAC unplugged etc.) via miniaudio's `ma_device_notification_proc`. On detection, auto-fail-over to the system default device, then fire `AudioEngine::audioDeviceChanged` with the new device's info. Listeners can override the fallback by calling `init(settings)` themselves. Without this, calling `init(settings)` to switch devices after a hot-unplug will hang in `ma_device_uninit` waiting for the dead device's audio thread to join. Likely also wants a `cause` enum on `AudioDeviceChangedArgs` (InitialInit / UserRequest / DeviceDisconnect) so listeners can distinguish event sources. | Medium |
| Custom-shader textured quad path (sgl-integrated) | A TrussC-level drawing path that issues `sg_apply_pipeline` + `sg_apply_bindings` + `sg_draw` with a user-supplied shader, while staying ordered with respect to surrounding `sokol_gl` commands. Built on the existing `suspendSwapchainPass()` / `resumeSwapchainPass()` machinery (already used by FBO), plus a partial-flush of the default sgl context so a `drawRect → font/customShader → drawCircle` sequence renders in submission order. Unlocks: R8 / RG8 / non-RGBA texture formats (current sgl shader forces RGBA8 with `(255,255,255,A)` padding), font atlas R8 storage (see below), sensor-data visualization (depth/IR/grayscale with palette mapping), SDF text, mask compositing, future Path glyph rendering. Probably surfaces as `Texture::drawWith(Shader&, ...)` or similar on the TrussC `gpu/` side; sgl_gl_tc fork ideally stays untouched. | High |
| Font atlas R8 storage | Drop font atlas from RGBA8 to R8 (4× memory reduction — e.g. ~4 MB → ~1 MB on a large CJK atlas). Currently blocked on the custom-shader path above: the sgl built-in shader returns `(R, 0, 0, 1)` from an R8 sample, which renders as solid red instead of `(color.rgb, glyph.a)`. Once that path lands, swap the atlas pixel format + add a tiny font-specific fragment shader doing `out = vec4(uColor.rgb, sample.r * uColor.a)`. No quality loss (glyphs were already grayscale alpha stored as `(255,255,255,A)`). Sokol R8 is native on all current backends (Metal / D3D11 / GL 3.3 / GLES3 / WebGL2 / WebGPU — GLES2 / WebGL1 already dropped). Pair with this PR cycle's tategaki work to make Japanese / CJK use cases cheap on RAM. | Low (once shader path exists) |
| Mouse event bubbling (press/release/drag) — **v0.7.0, breaking** | Unify input propagation with the scroll path, which already bubbles ("return false to allow bubbling to parent", see `RectNode::onMouseScroll`). Today `dispatchMousePress` delivers to the front-most hit node ONLY — if its `fireMousePress` returns false the event silently dies (no ancestor walk, no occluded-sibling fallback), so press/release/drag and scroll follow different rules. Plan: when the hit node does not consume, walk the parent chain (localizing via `localizeMouse` per level, firing node + mod hooks), and the first consumer becomes `grabbedNode` for drag tracking. Keep `RectNode::onMousePress`'s `return true` (consume) default, so bubbling activates only where a handler explicitly returns false — a path that today just swallows the event, making the change near-compatible in practice but still semantically breaking (hence the 0.7.0 window). Kills the current anti-pattern where a child must know its parent to forward input (contradicts the "dependencies point downward only" rule) and unblocks drag-through-children cases (grab a window by its title bar across label children; drag-scroll a list over its buttons). Watch mouse enter/leave consistency along the chain. **Considered and deferred, revisit after bubbling lands:** flipping hit-testing to default-ON ("visible RectNode catches clicks", DOM/Unity-style, opt-out via `disableEvents()`). Only sane as a package with bubbling + consume-by-choice (make `RectNode::onMousePress` return whether a listener consumed instead of unconditional true); default-ON alone under single-target dispatch would make every decorative label/separator child swallow its parent widget's clicks. Perf is a non-issue either way: the hit-test walk already visits all active+visible nodes (Mat4 inverse + children snapshot per node) and `eventsEnabled_` only gates the final rect test. | Medium |
| Font line wrapping (`enableWrap` + `setMaxLineLength`) | Opt-in text wrapping that flows long strings inside a length budget, default off so existing `drawString` behavior is unchanged. Staged rollout, all sharing the same internal "text → break-opportunity list → placement" split so later phases only refine the second half. **Phase 1**: basic wrap for horizontal text — break at ASCII spaces and between CJK ideographs (simplified UAX #14). **Phase 2**: Latin hyphenation — insert a `-` when a word has to be split mid-run, no dictionary, just last-resort. **Phase 3**: CJK `kinsoku` (行頭/行末禁則) opt-in via `setHangingPunctuation(true)` or similar — prevent `、。」』）` etc. from starting a line, either by hanging past the edge (ぶら下げ) or pulling back into the previous line (追い込み). Use a small kinsoku character set, not a full table. **Phase 4**: reuse the same pipeline for vertical writing — `maxLineLength` then means column height, columns break right→left as already implemented, kinsoku tables carry over. Justification / Knuth-Plass / advanced Western typography stays out of scope. Differentiator versus other creative-coding frameworks (oF / Cinder / Processing) where wrap is either absent or western-only. Likely lives on its own branch (`feat/font-wrap`) after the tategaki PR lands so the diffs stay reviewable. | Medium |

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
