# TrussC Design Document

## Loop Architecture (Decoupled Update/Draw)

TrussC uses a flexible loop architecture where Update (logic) and Draw (rendering) timing can be controlled. Two modes are available: **Synchronized Mode** and **Independent Mode**.

### Special FPS Constants

| Constant | Value | Behavior |
|----------|-------|----------|
| `tc::VSYNC` | -1.0f | Sync to monitor refresh rate |
| `tc::EVENT_DRIVEN` | 0.0f | Only on explicit `tc::redraw()` call |

### Synchronized Mode (Default)

Use `tc::setFps(fps)` for synchronized mode where `update()` is called exactly once before each `draw()`.

| Method | Behavior |
|--------|----------|
| `tc::setFps(VSYNC)` | VSync mode (default). Update and draw synced to monitor refresh |
| `tc::setFps(60)` | Fixed 60fps. Update/draw called together at 60Hz |
| `tc::setFps(EVENT_DRIVEN)` | Event-driven. Update/draw only on `tc::redraw()` |

### Independent Mode

Use `tc::setIndependentFps(updateFps, drawFps)` when you need different rates for update and draw.

| Example | Behavior |
|---------|----------|
| `tc::setIndependentFps(120, VSYNC)` | Update at 120Hz, draw at VSync |
| `tc::setIndependentFps(60, 30)` | Update at 60Hz, draw at 30fps |
| `tc::setIndependentFps(30, EVENT_DRIVEN)` | Update at 30Hz, draw only on redraw() |

**Note:** When `updateFps > drawFps` (e.g., 500Hz update with VSync draw), multiple update calls will burst before each draw, not evenly spaced.

### Getting Current Settings

```cpp
FpsSettings fps = tc::getFpsSettings();
// fps.updateFps   - Current update FPS setting (VSYNC, EVENT_DRIVEN, or fixed value)
// fps.drawFps     - Current draw FPS setting
// fps.synced      - true if update/draw are synchronized
// fps.actualVsyncFps - Monitor refresh rate (if available)

float actualFps = tc::getFps();  // Actual measured framerate
```

### Idle State (No Freeze)

- Even in event-driven mode, the OS event loop continues
- App doesn't freeze, enters "idle state"
- Event handlers like `onMousePress` still fire normally

### Examples

```cpp
// VSync mode (default behavior)
void tcApp::setup() {
    tc::setFps(VSYNC);
}

// Standard game loop (60fps fixed)
void tcApp::setup() {
    tc::setFps(60);
}

// Power-saving mode (event-driven)
void tcApp::setup() {
    tc::setFps(EVENT_DRIVEN);
}
void tcApp::mousePressed(int x, int y, int button) {
    // Process something
    tc::redraw();  // Explicitly request redraw
}

// Physics simulation (fixed timestep)
void tcApp::setup() {
    tc::setIndependentFps(120, VSYNC);  // Physics 120Hz, draw VSync
}

// UI application (save CPU, responsive updates)
void tcApp::setup() {
    tc::setIndependentFps(30, EVENT_DRIVEN);  // Animations at 30Hz, draw on demand
}
```

---

## 3D Projection

### Metal Clip Space

On macOS (Metal), clip space Z range differs from OpenGL:
- OpenGL: Z = [-1, 1]
- Metal: Z = [0, 1]

`sgl_ortho` generates OpenGL-style matrices, causing depth testing to malfunction on Metal.

### Solution

Use `sgl_perspective` for 3D drawing:

```cpp
tc::enable3DPerspective(fovY, nearZ, farZ);  // Perspective + depth test
// ... 3D drawing ...
tc::disable3D();  // Return to 2D
```

2D drawing continues using `sgl_ortho` (depth testing not needed).

---

## Lighting System (CPU-based Phong)

### Constraints and Design Decisions

sokol_gl is an immediate-mode API with the following limitations:
- Cannot pass normal attributes to shaders
- Cannot use custom shaders (built-in shaders only)

Therefore, **lighting calculations are performed on CPU and results are reflected in vertex colors**.

### Architecture

```
Light + Material + Normal
        ↓
  Phong calculation on CPU
        ↓
  Passed to sokol_gl as vertex colors
```

### Phong Lighting Model

```cpp
finalColor = emission + ambient + diffuse + specular

ambient  = lightAmbient × materialAmbient
diffuse  = lightDiffuse × materialDiffuse × max(0, N·L)
specular = lightSpecular × materialSpecular × pow(max(0, R·V), shininess)
```

- N: Normal vector (normalized)
- L: Light direction (normalized)
- R: Reflection vector
- V: View direction (normalized)

### Light Types

| Type | Description |
|------|-------------|
| Directional | Parallel light source (sunlight). Same direction hits all vertices |
| Point | Point light source (light bulb). Radiates from position with distance attenuation |
| ~~Spot~~ | Not implemented. Planned for future |

### Material Presets

```cpp
Material::gold()      // Gold
Material::silver()    // Silver
Material::copper()    // Copper
Material::bronze()    // Bronze
Material::emerald()   // Emerald
Material::ruby()      // Ruby
Material::plastic(color)  // Plastic (any color)
Material::rubber(color)   // Rubber (any color)
```

### Usage Example

```cpp
void tcApp::setup() {
    // Light setup
    light_.setDirectional(Vec3(-1, -1, -1));
    light_.setAmbient(0.2f, 0.2f, 0.25f);
    light_.setDiffuse(1.0f, 1.0f, 0.95f);

    // Material
    material_ = Material::gold();
}

void tcApp::draw() {
    tc::enable3DPerspective(fov, near, far);

    tc::enableLighting();
    tc::addLight(light_);
    tc::setMaterial(material_);

    sphere.draw();  // Lighting applied

    tc::disableLighting();
    tc::disable3D();
}
```

### Notes

- CPU calculation means high vertex counts increase load
- All vertices are recalculated each frame
- Specular calculation requires `setCameraPosition()`

---

## Module / Addon Architecture

TrussC divides functionality into "core modules" and "addons".

### Core Modules (tc:: namespace)

**Characteristics:**
- Available with just `#include <TrussC.h>`
- Implementation pre-compiled into `libTrussC`
- No additional build configuration for users

**Included Features:**
- Graphics (drawing, Image, FBO, Mesh, etc.)
- Math library (Vec, Mat, noise, FFT, etc.)
- Event system
- JSON / XML (nlohmann/json, pugixml)
- ImGui (Dear ImGui + sokol_imgui)
- Sound (sokol_audio + dr_libs)
- VideoGrabber (camera input)
- FileDialog

**Single-Header Library Handling:**

stb, dr_libs, nlohmann/json and other "single-header libraries" require `#define XXX_IMPLEMENTATION`. These are expanded once in TrussC implementation files (`src/*.cpp` / `src/*.mm`) and compiled into `libTrussC`. Users just include the header.

```
Example: stb_image.h

include/stb_image.h        ← Header (declarations only)
src/stb_impl.cpp           ← #define STB_IMAGE_IMPLEMENTATION + #include "stb_image.h"
```

**Benefits:**
- Faster compile times (implementation compiled once)
- Prevents link errors (avoids multiple definitions)
- Users just `#include` without thinking

### Addons (tcx:: namespace)

**Characteristics:**
- Requires additional CMake configuration
- May use external dependency libraries
- Optional or experimental features

**Future Candidates:**
- tcxOsc (Open Sound Control)
- tcxMidi (MIDI I/O)
- tcxOpenCV (image processing)
- tcxBox2D (physics)

**Usage:**

```cmake
# CMakeLists.txt
add_subdirectory(path/to/tcxOsc)
target_link_libraries(myApp PRIVATE tcx::Osc)
```

```cpp
// main.cpp
#include <TrussC.h>
#include <tcx/Osc.h>
```

### Decision Criteria

| Condition | Classification |
|-----------|---------------|
| Used in many projects | Core module |
| No external deps or MIT-compatible | Core module |
| Special purpose | Addon |
| Heavy external deps (OpenCV, etc.) | Addon |
| Experimental / unstable | Addon |
