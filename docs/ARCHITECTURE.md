# TrussC Architecture

## 1. Concept & Philosophy

**"Thin, Modern, and Native"**

A lightweight creative coding environment optimized for the AI-native and GPU-native era, suitable for commercial use.

| Principle | Description |
|-----------|-------------|
| **Minimalism** | Select only necessary features. No bloat. |
| **License Safety** | MIT / Zlib / Public Domain only. No GPL contamination. |
| **Transparency** | Thin wrappers over OS native APIs. Access to Metal / DX12 / Vulkan when needed. |
| **AI-Native** | Standard, predictable API design (C++20) that AI can easily generate code for. |

**Namespaces:**
- `tc::` - Core & Official Modules
- `tcx::` - Community Addons / Extensions

---

## 2. Tech Stack

Hybrid composition of native wrappers and high-quality lightweight libraries. See [BUILD_SYSTEM.md](BUILD_SYSTEM.md) for CMake details.

| Category | Library / Strategy | License |
|:---------|:-------------------|:--------|
| **Window/Input** | sokol_app | zlib |
| **Graphics** | sokol_gfx (Metal/DX12/Vulkan) | zlib |
| **Shader** | sokol-shdc (GLSL → Native) | zlib |
| **Math** | In-house (C++20 template) | MIT |
| **UI** | Dear ImGui | MIT |
| **Image** | stb_image, stb_image_write | Public Domain |
| **Font** | stb_truetype + fontstash | Public Domain |
| **Audio** | sokol_audio + dr_libs | zlib/PD |
| **Video/Camera** | OS Native (AVFoundation / Media Foundation) | - |
| **Serial** | OS Native (POSIX / Win32) | - |
| **Build** | CMake | - |

---

## 3. Directory Structure

```
MyProject/
├── CMakeLists.txt       # Build definition (shared template)
├── addons.make          # Addons to use (user edits this)
├── src/
│   ├── main.cpp         # Entry point
│   ├── tcApp.h          # User app definition
│   └── tcApp.cpp        # User app implementation
├── bin/
│   └── data/            # Assets (images, fonts, etc.)
└── icon/                # App icon (PNG, auto-converted)
```

---

## 4. API Design

### User Application

```cpp
// tcApp.h
#pragma once
#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
};
```

### Entry Point

```cpp
// main.cpp
#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.width = 1024;
    settings.height = 768;
    settings.title = "My TrussC App";

    runApp<tcApp>(settings);
    return 0;
}
```

### Global Helper Functions

```cpp
namespace tc {
    // Window
    int getWidth();
    int getHeight();
    void setFullscreen(bool full);
    void toggleFullscreen();

    // Mouse
    float getMouseX();
    float getMouseY();
    bool isMousePressed();

    // Time
    double getElapsedTime();
    double getDeltaTime();
    uint64_t getFrameNum();

    // Loop Control
    void setFps(float fps);
    void redraw();
}
```

---

## 5. Core Architecture

### A. Loop Modes

TrussC supports flexible frame rate control with synchronized and independent modes.

**Special Constants:**

| Constant | Value | Behavior |
|----------|-------|----------|
| `tc::VSYNC` | -1.0f | Sync to monitor refresh rate |
| `tc::EVENT_DRIVEN` | 0.0f | Only on explicit `tc::redraw()` call |

**Synchronized Mode (Default):**

```cpp
tc::setFps(VSYNC);       // VSync (default)
tc::setFps(60);          // Fixed 60fps
tc::setFps(EVENT_DRIVEN); // Event-driven (power saving)
```

**Independent Mode:**

```cpp
tc::setIndependentFps(120, VSYNC);  // Update 120Hz, draw VSync
tc::setIndependentFps(60, 30);      // Update 60Hz, draw 30fps
```

**Note:** In event-driven mode, the app doesn't freeze. Event handlers still fire normally.

### B. Scene Graph & Event System

- **tc::Node**: Base class with parent-child relationships and local transformation
- **Activation Control**: `isActive` stops node and all descendants completely
- **Visibility Control**: `isVisible` skips only draw (update/events continue)
- **Event Traverse**: Child nodes receive events even if parent has events disabled
- **Hit Test**: `hitTest(Ray)` in local coordinates, supports 2D/3D
- **Event Consumption**: Return `true` to consume event and stop propagation

### C. Timer System

Safe delayed execution on main thread, bound to Node lifecycle.

```cpp
// One-shot timer
this->callAfter(1.0, []{ cout << "1 second passed" << endl; });

// Repeating timer
this->callEvery(0.5, []{ cout << "Every 0.5 seconds" << endl; });
```

- Timers auto-destroyed when Node is deleted
- Zero overhead when no timers are active

### D. Rendering

**State Management:**

```cpp
tc::setBlendMode(BlendMode::Alpha);
tc::enableDepthTest();
```

**Scissor Clipping:**

Nodes can clip children to their bounds (axis-aligned only, performance priority).

**Graphics Context Stack:**

```cpp
tc::pushMatrix();
tc::translate(100, 100);
tc::rotate(TAU / 8.);
// ... draw ...
tc::popMatrix();

tc::pushStyle();
tc::setColor(1.f, 0, 0);
// ... draw ...
tc::popStyle();
```

**RAII Scoped Objects:**

```cpp
{
    ScopedMatrix m;
    tc::translate(100, 100);
    // auto pop on scope exit
}
```

### E. Threading

Based on C++ standard features:

- Use `std::mutex` and `std::lock_guard` for synchronization
- `tc::Thread` class wraps `std::thread` for lifecycle management

### F. Console & AI Automation

TrussC apps accept commands via stdin and output events to stdout, enabling:

- **AI agent control** - Claude, GPT can simulate input
- **Automated testing** - Script UI interactions
- **Operation replay** - Capture and replay user actions

```bash
# Get app status
echo 'tcdebug info' | ./myapp

# Simulate click (requires enableDebugInput)
echo 'tcdebug {"type":"mouse_click","x":100,"y":200}' | ./myapp

# Capture user input
echo 'tcdebug stream normal' | ./myapp
```

See [AI_AUTOMATION.md](AI_AUTOMATION.md) for full command reference.

---

## 6. 3D Graphics

### Metal Clip Space

Metal uses Z range [0, 1] vs OpenGL's [-1, 1]. Use perspective projection for 3D:

```cpp
tc::enable3DPerspective(fovY, nearZ, farZ);
// ... 3D drawing ...
tc::disable3D();
```

### Lighting System (CPU-based Phong)

sokol_gl cannot pass normals to shaders, so lighting is calculated on CPU.

```cpp
void tcApp::setup() {
    light_.setDirectional(Vec3(-1, -1, -1));
    material_ = Material::gold();
}

void tcApp::draw() {
    tc::enable3DPerspective(fov, near, far);
    tc::enableLighting();
    tc::addLight(light_);
    tc::setMaterial(material_);

    sphere.draw();

    tc::disableLighting();
    tc::disable3D();
}
```

**Light Types:** Directional, Point (Spot planned)

**Material Presets:** `Material::gold()`, `Material::silver()`, `Material::plastic(color)`, etc.

---

## 7. Addons

TrussC can be extended with addons (similar to openFrameworks' ofxAddon).

- **Core modules** (`tc::`): Built into libTrussC, always available
- **Addons** (`tcx::`): Optional, require `addons.make` configuration

See [ADDONS.md](ADDONS.md) for details on using and creating addons.

---

## 8. Class Design Policy

### Infrastructure Objects

Policy: Use virtual, encourage inheritance/extension.

- **Examples:** `tc::Node`, `tc::App`, `tc::Window`, `tc::VideoPlayer`
- **Memory:** `std::shared_ptr` automatic management

### Data Objects

Policy: Prioritize performance and memory layout (POD-ness).

- **Examples:** `tc::Vec3`, `tc::Color`, `tc::Matrix`, `tc::Mesh`
- **Memory:** Value semantics, no virtual methods

---

## 9. Native Wrapper Strategy

To avoid GPL contamination and size bloat, these features wrap OS-specific APIs:

### Video & Camera (No FFmpeg)

- **macOS:** AVFoundation + CVMetalTextureCache (zero-copy)
- **Windows:** Media Foundation + Direct3D 11 texture

### Serial Communication (No Boost)

- **macOS/Linux:** POSIX (`open()`, `tcsetattr()`, `read()`)
- **Windows:** Win32 (`CreateFile()`, `SetCommState()`, `ReadFile()`)
