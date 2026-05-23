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

### Medium Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| VBO detail control | Dynamic vertex buffers | Medium |
| macOS deprecated API migration | Replace `tracksWithMediaType:` / `copyCGImageAtTime:` with async equivalents (deprecated in macOS 15.0) | Medium |
| `SG_VERTEXFORMAT_INT10_N2` adoption | sokol_gfx (2026-05) added a 10-10-10-2 normalized int vertex format. Adopt for `tcMesh` normal / tangent attributes — 3x smaller than FLOAT3 with effectively no visual loss (Unity / Unreal default). D3D11 backend not yet supported upstream, so verify Windows path before committing. | Medium |
| Configurable 10-bit color output | TrussC currently forces RGB10A2 swap-chain in sokol_app patches. Make it opt-in via WindowSettings once upstream sokol adds a `SAPP_PIXELFORMAT_RGB10A2` (currently not in upstream — track [floooh/sokol](https://github.com/floooh/sokol)). | Low |
| Dear ImGui 1.92.6 → latest | Currently on 1.92.6 WIP. 1.92.8 (2026-05-12) introduced a notable breaking change: `AddRect()` / `AddPolyline()` / `PathStroke()` swapped their `flags` and `thickness` arg positions. TrussC core does not touch these, but user code that does will need updates. Inline redirection keeps source compatible unless `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` is on. Plan a single bump to the latest 1.92.x. | Low |


---

## oF Compatibility Gap

Features available in openFrameworks but missing in TrussC.

| Category | Method | Description | Priority |
|:---------|:-------|:------------|:---------|
| Image | `crop` / `resize` / `mirror` | Basic image manipulation | Medium |
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
