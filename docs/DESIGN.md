# TrussC Design Document

## Loop Architecture (Decoupled Update/Draw)

Replaces the "mode enumeration" approach with a method-based architecture where Draw (rendering) and Update (logic) timing can be independently configured.

### Draw Loop (Render Timing)

| Method | Behavior |
|--------|----------|
| `tc::setDrawVsync(true)` | Sync to monitor refresh rate (default) |
| `tc::setDrawFps(n)` (n > 0) | Fixed FPS. VSync is automatically disabled |
| `tc::setDrawFps(0)` (or negative) | Auto loop stops. Draws only on `tc::redraw()` |

### Update Loop (Logic Timing)

| Method | Behavior |
|--------|----------|
| `tc::syncUpdateToDraw(true)` | `update()` called once just before `draw()` (Coupled, default) |
| `tc::setUpdateFps(n)` (n > 0) | `update()` runs independently at specified Hz (Decoupled) |
| `tc::setUpdateFps(0)` (or negative) | Update loop stops. Fully event-driven app |

### Idle State (No Freeze)

- Even when both Draw and Update are stopped (FPS <= 0), the OS event loop continues
- App doesn't freeze, enters "idle state"
- Event handlers like `onMousePress` still fire normally

### Standard Helpers

| Helper | Behavior |
|--------|----------|
| `tc::setFps(60)` | Executes `setDrawFps(60)` + `syncUpdateToDraw(true)` |
| `tc::setVsync(true)` | Executes `setDrawVsync(true)` + `syncUpdateToDraw(true)` |

### Examples

```cpp
// Standard game loop (60fps fixed)
void tcApp::setup() {
    tc::setFps(60);
}

// VSync sync (default behavior)
void tcApp::setup() {
    tc::setVsync(true);
}

// Power-saving mode (event-driven)
void tcApp::setup() {
    tc::setDrawFps(0);  // Stop drawing
}
void tcApp::onMousePress(...) {
    // Process something
    tc::redraw();  // Explicitly request redraw
}

// Physics simulation (fixed timestep)
void tcApp::setup() {
    tc::setDrawVsync(true);    // Draw with VSync
    tc::setUpdateFps(120);     // Physics at 120Hz
}
```

### Current Implementation (Deprecated)

```cpp
// Old API (deprecated)
enum class LoopMode {
    Game,   // update/draw called automatically every frame
    Demand  // update/draw called only on redraw()
};
tc::setLoopMode(LoopMode::Game);
tc::setLoopMode(LoopMode::Demand);
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
