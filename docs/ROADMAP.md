# TrussC Roadmap

Development roadmap based on feature comparison with openFrameworks.

---

## Implemented Features

### Graphics (Drawing)
- [x] Basic shapes (rect, circle, ellipse, line, triangle, point)
- [x] Image (load, draw, save)
- [x] BitmapFont (texture atlas)
- [x] TrueTypeFont (stb_truetype based)
- [x] Shape API (beginShape/vertex/endShape)
- [x] Path (vertex arrays, curve generation, Bezier curves, arcs) *formerly Polyline*
- [x] Mesh (vertices, colors, indices, normals)
- [x] StrokeMesh (thick line drawing)
- [x] Scissor Clipping (recursive support)
- [x] Blend modes (Alpha, Add, Multiply, Screen, Subtract, Disabled)

### 3D
- [x] 3D transforms (translate/rotate/scale)
- [x] Depth testing, back-face culling
- [x] 3D primitives (Plane, Box, Sphere, Cylinder, Cone, Torus, IcoSphere)
- [x] EasyCam (mouse-controlled 3D camera)
- [x] Node (scene graph)
- [x] RectNode (2D UI, Ray-based Hit Test)
- [x] Lighting (Ambient, Diffuse, Specular / Phong model)
- [x] Materials (presets: gold, silver, copper, emerald, etc.)
- [x] Light (Directional, Point)

### Math
- [x] Vec2, Vec3, Vec4
- [x] Mat3, Mat4
- [x] Perlin Noise (1D-4D)
- [x] Ray (for Hit Test)
- [x] Color spaces (RGB, HSB, OKLab, OKLCH)
- [x] Utilities (lerp, clamp, map, radians, degrees)

### Events
- [x] Keyboard (keyPressed, keyReleased)
- [x] Mouse (pressed, released, moved, dragged, scrolled)
- [x] Window resize
- [x] Event<T> template
- [x] EventListener (RAII)
- [x] RectNode events (mousePressed/Released/Dragged/Scrolled)
- [x] Drag & drop (file receiving)

### GL
- [x] Shader (fullscreen shaders)
- [x] FBO (frame buffer object)
- [x] Texture detail control (Filter: Nearest/Linear, Wrap: Repeat/MirroredRepeat/ClampToEdge)

### Utils
- [x] Timer (Node::addTimerFunction)
- [x] Thread, ThreadChannel
- [x] Serial communication
- [x] Frame control (FPS/VSync)
- [x] Log (tcLog: Verbose/Notice/Warning/Error/Fatal)
- [x] JSON / XML (nlohmann/json, pugixml)
- [x] File dialogs (OS native)
- [x] Network (TCP/UDP)

### Addons (Optional)
- [x] tcxTls - TLS/SSL communication (mbedTLS)
- [x] tcxOsc - OSC protocol
- [x] tcxBox2d - Box2D physics engine

### Sound
- [x] Sound (sokol_audio + dr_libs)
- [x] SoundStream (audio input)

### Video
- [x] VideoGrabber (webcam input)

### UI
- [x] Dear ImGui integration

---

## Unimplemented Features (by priority)

### Priority: Medium

| Feature | Description | Difficulty |
|---------|-------------|------------|
| 3D model loading | obj/gltf | High |
| Texture mapping | Texture application to Mesh | Medium |
| Normal mapping | Bump mapping | High |
| Video playback | Video file playback | High |

### Priority: Low

| Feature | Description | Difficulty |
|---------|-------------|------------|
| VBO detail control | Dynamic vertex buffers | Medium |
| Particle system | Consider as addon | Medium |
| Touch input | iOS/Android | High |
| Spot light | Spotlight support | Medium |

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
| sokol | Rendering backend | Medium | Watch for API changes, **TrussC has customizations (see below)** |
| miniaudio | Audio | Medium | |
| Dear ImGui | GUI | Low | Use stable versions |

**Update Checklist:**
- Check GitHub Release Notes / Security Advisories
- For stb, check commit history directly at https://github.com/nothings/stb (no tags)

**sokol Update Notes (TrussC Customizations):**

`sokol_app.h` has customizations to prevent flickering during event-driven rendering.
When updating sokol, these changes must be reapplied.

1. Add `bool skip_present;` flag to `_sapp_t` struct
2. Add API declaration `SOKOL_APP_API_DECL void sapp_skip_present(void);`
3. Add skip check at top of `_sapp_d3d11_present()`
4. Add `sapp_skip_present()` implementation

Details via git diff: `git log --oneline -p -- trussc/include/sokol/sokol_app.h`

---

## Known Issues

### Windows Specific

| Issue | Description | Solution |
|-------|-------------|----------|
| ~~Console window~~ | ~~Command prompt appears behind when double-clicking executable~~ | ✅ Resolved: Hidden in Release, show with `TRUSSC_SHOW_CONSOLE` |
| ~~Icon not applied~~ | ~~.ico not applied to executable~~ | ✅ Resolved: `trussc_setup_icon()` auto-generates .rc file |

### Cross-Platform

| Issue | Description | Solution |
|-------|-------------|----------|
| ~~Event-based drawing flicker~~ | ~~Single frame updates via redraw() cause flickering due to double buffering~~ | ✅ Resolved: Added `sapp_skip_present()` to sokol_app, skips Present when drawing is skipped |

---

## Windows / Linux Support Status

Currently developed on macOS (Metal). Porting status for Windows / Linux.

### ✅ Supported (Cross-Platform)

| Feature | Implementation | Notes |
|:--------|:---------------|:------|
| **Core (sokol)** | sokol_app / sokol_gfx | Metal / D3D11 / OpenGL / Vulkan / WebGPU |
| **FileDialog** | mac.mm / win.cpp / linux.cpp | OS native dialogs |
| **UDP Socket** | `#ifdef` branching | Winsock / POSIX both supported |
| **TCP Client/Server** | `#ifdef` branching | Winsock / POSIX both supported |
| **Sound** | miniaudio | Cross-platform |
| **ImGui** | - | Cross-platform |
| **Serial** | `tcSerial.h` | Win32 API / POSIX both supported |
| **Platform** | `tcPlatform_win.cpp` | getExecutablePath, setWindowSize, getDisplayScaleFactor, captureWindow, saveScreenshot |
| **FBO** | `tcFbo_win.cpp` | D3D11 Map/Unmap for pixel reading |
| **VideoGrabber** | `tcVideoGrabber_win.cpp` | Media Foundation for webcam |

### ❌ macOS Only → Linux Implementation Needed

| Feature | File | Linux |
|:--------|:-----|:------|
| **Platform** | `tcPlatform_linux.cpp` | |
| ├ getDisplayScaleFactor | | X11: `XRRGetScreenResources` |
| ├ setWindowSize | | X11: `XResizeWindow` |
| ├ getExecutablePath | | `/proc/self/exe` |
| ├ captureWindow | | OpenGL `glReadPixels` |
| └ saveScreenshot | | Can substitute with stb_image_write |
| **FBO** | `tcFbo_linux.cpp` | OpenGL `glReadPixels` |
| **VideoGrabber** | `tcVideoGrabber_linux.cpp` | V4L2 |

### Linux Porting Priority

**Medium (used by some):**
1. `tcFbo_linux.cpp` - FBO pixel reading
2. `tcVideoGrabber_linux.cpp` - Camera input
3. `tcPlatform_linux.cpp` - Platform functions

---

## Platform-Specific Test Checklist

List of samples/features requiring focused testing due to OS-specific code.

### Windows Test Items

**🔴 Highest Priority (OS-specific code heavy)**

| Sample | Check Points | Status |
|--------|--------------|--------|
| network/tcpExample | Winsock connection, disconnection, error handling | ✅ Verified |
| network/udpExample | Winsock, broadcast, multicast | ✅ Verified |
| input_output/screenshotExample | D3D11 texture capture (`tcPlatform_win.cpp`) | ✅ Verified (MSAA fix applied) |
| input_output/fileDialogExample | Win32 IFileDialog (`tcFileDialog_win.cpp`) | ✅ Verified |
| video/videoGrabberExample | Media Foundation API (`tcVideoGrabber_win.cpp`) | ⬜ Untested (no camera available) |

**🟡 Needs Verification (platform/win/ implementation)**

| Feature | File | Sample | Status |
|---------|------|--------|--------|
| FBO pixel reading | `tcFbo_win.cpp` | graphics/fboExample | ✅ Verified |
| DPI scaling | `tcPlatform_win.cpp` | All samples | ⬜ Untested |
| Executable path | `tcPlatform_win.cpp` | Samples using dataPath | ✅ Verified |
| Console UTF-8 | sokol_app.h | All log output | ✅ Verified |

**🟢 Relatively Safe (cross-platform libraries)**

- graphics - sokol handles
- sound - miniaudio handles
- imgui - sokol_imgui handles

### Linux Test Items

**🔴 Not Implemented (needs creation)**

| Feature | File | Implementation Needed | Status |
|---------|------|----------------------|--------|
| Platform basic functions | `tcPlatform_linux.cpp` | Currently stubs only | ⬜ Not started |
| ├ getDisplayScaleFactor | | X11 `XRRGetScreenResources` | ⬜ |
| ├ setWindowSize | | X11 `XResizeWindow` | ⬜ |
| ├ getExecutablePath | | `/proc/self/exe` readlink | ⬜ |
| ├ captureWindow | | OpenGL `glReadPixels` | ⬜ |
| └ saveScreenshot | | stb_image_write | ⬜ |
| FBO pixel reading | `tcFbo_linux.cpp` | OpenGL `glReadPixels` | ⬜ Not started |
| VideoGrabber | `tcVideoGrabber_linux.cpp` | V4L2 | ⬜ Not started |

**🟡 Needs Verification (POSIX code)**

| Sample | Check Points | Status |
|--------|--------------|--------|
| network/tcpExample | POSIX sockets | ⬜ Untested |
| network/udpExample | POSIX sockets | ⬜ Untested |
| communication/serialExample | POSIX termios | ⬜ Untested |
| input_output/fileDialogExample | GTK3 dialog (`tcFileDialog_linux.cpp`) | ⬜ Untested |

**🟢 Expected to Work (cross-platform)**

- graphics - sokol OpenGL Core handles
- sound - miniaudio ALSA/PulseAudio support
- imgui - sokol_imgui handles

### Web (Emscripten) Test Items

**✅ Basic Functionality Verified**

| Feature | Status |
|---------|--------|
| Drawing (WebGL2) | ✅ Works |
| Resize | ✅ Works (custom shell) |
| Fullscreen | ✅ Works |

**⬜ Untested**

| Feature | Notes |
|---------|-------|
| Keyboard input | |
| Mouse input | |
| ImGui | |
| Sound (miniaudio) | May need WebAudio support |
| Network | Needs WebSocket conversion |

---

## Sample List

### Implemented

| Category | Samples |
|----------|---------|
| templates/ | emptyExample |
| graphics/ | graphicsExample, colorExample, clippingExample, blendingExample, fontExample, polylinesExample, strokeMeshExample, fboExample, shaderExample, textureExample |
| 3d/ | ofNodeExample, 3DPrimitivesExample (with lighting), easyCamExample |
| math/ | vectorMathExample, noiseField2dExample |
| events/ | eventsExample, hitTestExample, uiExample |
| input_output/ | fileDialogExample, imageLoaderExample, screenshotExample, dragDropExample, jsonXmlExample, keyboardExample, mouseExample |
| sound/ | soundPlayerExample, soundPlayerFFTExample, micInputExample |
| video/ | videoGrabberExample |
| network/ | tcpExample, udpExample |
| communication/ | serialExample |
| gui/ | imguiExample |
| threads/ | threadExample, threadChannelExample |
| windowing/ | loopModeExample |

### Future

| Category | Sample | Description | Priority |
|----------|--------|-------------|----------|
| 3d/ | modelLoaderExample | OBJ/glTF model loading | Medium |
| graphics/ | particleExample | Particle system | Medium |
| animation/ | tweenExample | Value interpolation with easing (linear, ease-in/out, bounce, etc.) | Medium |
| animation/ | spriteSheetExample | Sprite sheet animation | Low |
| game/ | pongExample | Simple game demo | Low |
| generative/ | flowFieldExample | Generative art with flow fields | Low |
| generative/ | lSystemExample | L-System tree/plant generation | Low |

---

## Raspberry Pi Support (Planned)

Priority: Low | Difficulty: Medium

### Investigation Phase
- [ ] Check sokol Raspberry Pi support (OpenGL ES backend)
- [ ] Decide whether to support RPi 4/5 only or include RPi 3 and earlier
- [ ] Identify required Linux packages (libgl, libegl, x11-dev, etc.)

### Build System
- [ ] Add ARM architecture detection to CMakeLists.txt
- [ ] Add OpenGL ES force flag (`SOKOL_GLES2` or `SOKOL_GLES3`)
- [ ] Cross-compilation support (optional, build for RPi from Mac/PC)

### Platform Code Verification
- [ ] `tcPlatform_linux.cpp` - Verify on ARM
- [ ] `tcFbo_linux.cpp` - Verify glReadPixels with OpenGL ES
- [ ] `tcVideoGrabber_linux.cpp` - Verify V4L2 with RPi camera

### Dependency Verification
- [ ] miniaudio - Verify ALSA backend on ARM
- [ ] stb_* - Header-only, should be OK
- [ ] Dear ImGui - Verify OpenGL ES support

### Testing
- [ ] Test on actual hardware (RPi 4 or 5)
- [ ] Verify basic sample (graphicsExample) runs
- [ ] Performance measurement

### Documentation
- [ ] Add RPi setup instructions
- [ ] Document known limitations

---

## Reference Links

- [oF Examples](https://github.com/openframeworks/openFrameworks/tree/master/examples)
- [oF Documentation](https://openframeworks.cc/documentation/)
- [sokol](https://github.com/floooh/sokol)
