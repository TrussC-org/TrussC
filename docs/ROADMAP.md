# TrussC Roadmap

---

## Current Status

**Platforms:** macOS, Windows, Linux, Web (Emscripten)

### Implemented Features

| Category | Features |
|----------|----------|
| **Graphics** | Shapes, Image, Fonts (Bitmap/TrueType), Path, Mesh, StrokeMesh, Clipping, Blend modes, Texture mapping (UV) |
| **3D** | Transforms, Primitives, EasyCam, Node, RectNode, Phong Lighting (Directional/Point), Materials (Specular) |
| **GPU** | Shader, FBO, Texture |
| **Math** | Vec2/3/4, Mat3/4, Perlin Noise, Ray, Color spaces (RGB/HSB/OKLab/OKLCH) |
| **Events** | Keyboard, Mouse, Window resize, Drag & drop, Event<T>, RectNode events |
| **Sound** | SoundPlayer, SoundStream (mic input), ChipSound, AAC decoding (macOS/Linux) |
| **Video** | VideoPlayer (FFmpeg), VideoGrabber (webcam) |
| **Network** | TCP Client/Server, UDP |
| **Utils** | Timer, Thread, Serial, File dialogs, JSON/XML, Clipboard |
| **Addons** | tcxImGui (Dear ImGui), tcxTls (TLS/SSL), tcxOsc (OSC), tcxBox2d (physics), tcxWebSocket |

---

## Planned Features

### High Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| Component system | Attachable behaviors for Node (Layout, Draggable, etc.) | Medium |
| UI Layout | VStack/HStack/Flex layout components | Medium |
| 3D model loading | OBJ/glTF loader | High |
| Spot light | Spotlight support for lighting system | Medium |

### Medium Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| Normal mapping | Bump mapping with normal textures | High |
| VBO detail control | Dynamic vertex buffers | Medium |
| Raspberry Pi | ARM/OpenGL ES support | Medium |

### Low Priority

| Feature | Description | Difficulty |
|---------|-------------|------------|
| Touch input | iOS/Android support | High |

---

## oF Compatibility Gap

Features available in openFrameworks but missing in TrussC.

| Category | Method | Description | Priority |
|:---------|:-------|:------------|:---------|
| Graphics | `drawRoundedRect` | Rounded rectangles for UI | **High** |
| 3D | `worldToScreen` / `screenToWorld` | Coordinate conversion for 3D picking | **High** |
| Image | `crop` / `resize` / `mirror` | Basic image manipulation | Medium |
| System | `launchBrowser` | Open URL in default browser | Medium |
| 3D | `drawPlane` / `drawCylinder` | Additional basic 3D primitives | Medium |
| Math | `ofWrap` / `ofAngleDifference` | Floating point wrapping and angle helpers | Low |

---

## Future Samples

| Category | Sample | Description |
|----------|--------|-------------|
| ui/ | layoutExample | VStack/HStack layout with components |
| ui/ | componentExample | Draggable, ScrollBehavior demo |
| 3d/ | modelLoaderExample | OBJ/glTF model loading |
| animation/ | spriteSheetExample | Sprite sheet animation |
| game/ | pongExample | Simple game demo |
| generative/ | flowFieldExample | Generative art with flow fields |

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

## Raspberry Pi Support

Priority: Low | Difficulty: Medium

### TODO
- [ ] Check sokol OpenGL ES backend support
- [ ] Decide RPi version support (4/5 only or include 3)
- [ ] Add ARM architecture detection to CMake
- [ ] Test V4L2 with RPi camera
- [ ] Verify miniaudio ALSA on ARM
- [ ] Test on actual hardware

---

## Component System Design

Priority: High | Difficulty: Medium

### Concept

Attach reusable behaviors to Node without modifying Node class itself.
Unlike child nodes, Components:
- Don't participate in hit testing
- Don't get affected by layout
- Have automatic lifecycle management (setup/update/draw/destroy)
- Can query owner node

### Base Class

```cpp
class Component {
protected:
    Node* owner_ = nullptr;

    virtual void setup() {}
    virtual void update() {}
    virtual void draw() {}
    virtual void onDestroy() {}

public:
    Node* getOwner() { return owner_; }
};
```

### Node Integration

```cpp
class Node {
    vector<unique_ptr<Component>> components_;

public:
    template<typename T, typename... Args>
    T* addComponent(Args&&... args);

    template<typename T>
    T* getComponent();

    template<typename T>
    void removeComponent();
};
```

### Use Cases

| Component | Description |
|-----------|-------------|
| VStackLayout | Auto-arrange children vertically |
| HStackLayout | Auto-arrange children horizontally |
| Draggable | Make node draggable with mouse |
| ScrollBehavior | Scroll children with mouse wheel |
| SpriteAnimator | Animate sprite sheets |
| AudioEmitter | Spatial audio source |

### Example Usage

```cpp
auto panel = make_shared<RectNode>(400, 300);
panel->addComponent<VStackLayout>(10);  // gap = 10

panel->addChild(button1);  // auto-positioned at y=0
panel->addChild(button2);  // auto-positioned at y=button1.height+10
panel->addChild(button3);  // auto-positioned at y=...

// Draggable in one line
panel->addComponent<Draggable>();
```

### Benefits over External Helpers

- Lifecycle managed by Node (no manual update() calls)
- Automatic cleanup when Node is destroyed
- Standard `getComponent<T>()` query pattern
- Serialization: save/load node with all components

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
| Dear ImGui | GUI (tcxImGui addon) | Low | Use stable versions |

**Update Checklist:**
- Check GitHub Release Notes / Security Advisories
- For stb, check commit history at https://github.com/nothings/stb (no tags)

### sokol Customizations

`sokol_app.h` has TrussC-specific modifications. When updating sokol, these changes must be reapplied.

See: [`trussc/include/sokol/TRUSSC_MODIFICATIONS.md`](../trussc/include/sokol/TRUSSC_MODIFICATIONS.md)

---

## Implemented Samples

| Category | Samples |
|----------|---------|
| templates/ | emptyExample |
| graphics/ | graphicsExample, colorExample, clippingExample, blendingExample, fontExample, polylinesExample, strokeMeshExample, fboExample, shaderExample, textureExample |
| 3d/ | ofNodeExample, 3DPrimitivesExample, easyCamExample |
| math/ | vectorMathExample, noiseField2dExample |
| events/ | eventsExample, hitTestExample, uiExample |
| input_output/ | fileDialogExample, imageLoaderExample, screenshotExample, dragDropExample, jsonXmlExample, keyboardExample, mouseExample |
| sound/ | soundPlayerExample, soundPlayerFFTExample, micInputExample |
| video/ | videoGrabberExample |
| network/ | tcpExample, udpExample |
| communication/ | serialExample |
| ~~gui/~~ | ~~imguiExample~~ (moved to addons/tcxImGui/example-basic) |
| threads/ | threadExample, threadChannelExample |
| windowing/ | loopModeExample |
| animation/ | tweenExample |

---

## Reference Links

- [oF Examples](https://github.com/openframeworks/openFrameworks/tree/master/examples)
- [oF Documentation](https://openframeworks.cc/documentation/)
- [sokol](https://github.com/floooh/sokol)
