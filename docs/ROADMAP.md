# TrussC Roadmap

---

## Current Status

**Platforms:** macOS, Windows, Linux, Web (Emscripten), Raspberry Pi, iOS, Android


## Planned Features

### Implemented (gpu-pbr branch)

| Feature | Description |
|---------|-------------|
| GPU PBR lighting | Cook-Torrance GGX BRDF, metallic-roughness workflow, up to 8 lights |
| Light types | Directional, Point, Spot with cone falloff |
| Projector light | Texture projection with lens shift, aspect ratio, gobo |
| IES profiles | IESNA LM-63 photometric profiles for angular intensity |
| IBL environment | HDR / procedural sky, irradiance + prefilter + BRDF LUT |
| Normal mapping | Tangent-space normal maps with TBN matrix |
| PBR texture maps | Base color, metallic-roughness, emissive, occlusion (glTF 2.0) |
| Shadow mapping | R32F depth map, 3x3 PCF, per-light bias control |
| glTF loader | tcxGltf addon (cgltf), GLB/glTF with embedded textures |
| ImGui addon extraction | Extracted to tcxImGui addon |

### High Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| Multi-light shadows | Support shadow maps for multiple lights simultaneously | High |
| Area lights | Rectangle / disc / line area light sources | High |
| Cascaded shadow maps | CSM for directional lights (large outdoor scenes) | High |

### Medium Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| FLAC support | Enable FLAC decoding via miniaudio configuration | Low |
| VBO detail control | Dynamic vertex buffers | Medium |
| macOS deprecated API migration | Replace `tracksWithMediaType:` / `copyCGImageAtTime:` with async equivalents (deprecated in macOS 15.0) | Medium |
| PBR in Fbo | Allow PBR mesh rendering inside Fbo passes | Medium |


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
| **stb_truetype** | Font rendering | Medium | |
| pugixml | XML parsing | Medium | |
| nlohmann/json | JSON parsing | Medium | |
| sokol | Rendering backend | Medium | **TrussC has customizations (see below)** |
| miniaudio | Audio | Medium | |
| Dear ImGui | GUI | Low | Use stable versions |

**Update Checklist:**
- Check GitHub Release Notes / Security Advisories
- For stb, check commit history at https://github.com/nothings/stb (no tags)

### sokol Customizations

`sokol_app.h` has TrussC-specific modifications. When updating sokol, these changes must be reapplied.

See: [`trussc/include/sokol/TRUSSC_MODIFICATIONS.md`](../trussc/include/sokol/TRUSSC_MODIFICATIONS.md)

---

## Reference Links

- [oF Examples](https://github.com/openframeworks/openFrameworks/tree/master/examples)
- [oF Documentation](https://openframeworks.cc/documentation/)
- [sokol](https://github.com/floooh/sokol)
