# **TrussC Design Document**

## **1. Concept & Philosophy**

**"Thin, Modern, and Native"**

A lightweight creative coding environment optimized for the AI-native and GPU-native era, suitable for commercial use.

* **Name:** **TrussC** (Truss + C [Core/Creative/Code/C++])
* **Namespace:**
  * tc:: (Core & Official Modules)
  * tcx:: (Community Addons / Extensions)
* **Minimalism (No Bloat):**
  * Select only necessary features to keep binary size minimal.
  * Exclude unnecessary libraries from compiled artifacts.
* **License Safety (No GPL):**
  * Designed for commercial use, composed only of MIT / Zlib / Public Domain licenses.
  * Eliminates dependencies on FFmpeg or FreeType that cause LGPL/GPL contamination.
* **Transparency (No Black Box):**
  * "Thin" design that provides easy access to OS native APIs without heavy wrappers.
  * Leaves room to directly access Metal / DirectX 12 / Vulkan features.
* **AI-Native:**
  * Standard, predictable API design (C++20 compliant) that AI (LLMs) can easily generate code for.
  * Avoids overly unique syntax, adopts modern namespaces.

## **2. Tech Stack**

Hybrid composition of "Native Wrappers (in-house implementation)" and "high-quality lightweight libraries (header-only)".

| Category | Function | Library / Strategy | License |
|:---------|:---------|:-------------------|:--------|
| **Core** | Window, input, context | **sokol_app** | zlib |
| **Graphics** | Rendering backend (Metal/DX12/VK) | **sokol_gfx** | zlib |
| **Scene** | Scene graph, event propagation | **In-house (Node System)** | MIT |
| **Shader** | Shader management/conversion | **sokol-shdc** (GLSL -> Native) | zlib |
| **Math** | Vector/matrix operations | **In-house (C++20 template)** or HandmadeMath | Public Domain |
| **UI** | Dev GUI, debug tools | **Dear ImGui** (provided as addon) | MIT |
| **Image** | Image loading/saving | **stb_image**, **stb_image_write** | Public Domain |
| **Font** | Font rendering | **stb_truetype** + **fontstash** | Public Domain |
| **Audio** | Audio I/O, file playback | **sokol_audio** + **dr_libs** (mp3/wav) | zlib/PD |
| **Video** | Video playback, camera input | **OS Native API Wrapper (in-house)** (AVFoundation / Media Foundation) | - |
| **Comms** | Serial communication | **OS Native API Wrapper (in-house)** (Win32 API / POSIX) | - |
| **Build** | Build system | **CMake** (projectGenerator discontinued) | - |

## **3. Directory Structure**

Directory structure that eliminates oF's "hierarchy constraints" and ensures portability.

MyProject/
├── CMakeLists.txt       # Build definition file
├── src/
│   ├── main.cpp         # Entry point
│   ├── tcApp.h          # User app definition (header)
│   └── tcApp.cpp        # User app implementation (source)
├── bin/
│   └── data/            # Assets (images, shaders, fonts, etc.)
├── libs/                # Framework itself
│   └── TrussC/
└── addons/              # Addons (auto-detected by CMake)
    ├── tcImGui/
    │   └── CMakeLists.txt
    └── tcOsc/

### **Addon Guidelines**

**1. Naming Convention (Namespace Collision Avoidance):**

* **tcPrefix (No 'x'):** Addon folder names use tcName (e.g.: tcOrbbec).
* **Sub-Namespaces:** Use sub-namespaces for addons wrapping specific libraries to prevent class name collisions.
  * Bad: class tc::Device (collision)
  * Good: namespace tc::orb { class Device; } (safe)

**2. Build Logic (CMake):**

* Each addon has its own CMakeLists.txt.
* OS branching via if(WIN32), if(APPLE), etc. is possible.
* Using add_custom_command(POST_BUILD ...) allows **automatic copying** of DLLs and assets to the executable directory.
* This lets users simply write trussc_use_addon(tcOrbbec) without complex environment setup.

Environment Setup Automation Logic (CMake):
Users can build without worrying about placement.

1. Search for libs/ within the project.
2. If not found, reference the TRUSSC_PATH environment variable.
3. (Optional) If not found, auto-download from GitHub via FetchContent.

## **4. API Design**

Intuitive, object-oriented structure through tc::App class inheritance.
Since tc::App itself inherits from tc::Node, the application itself functions as the root of the scene graph.

### **1. User Application (tcApp.h)**

Users create a class inheriting from tc::App. Just override setup, update, draw.

```cpp
#pragma once
#include "TrussC.h"

// tc::App inherits from tc::Node!
// So addChild() and this->setPosition() work directly.
class tcApp : public tc::App {
    std::shared_ptr<tc::Texture> image;
    float angle = 0.0f;

public:
    void setup() override;
    void update() override;
    void draw() override;
};
```

### **2. Implementation (tcApp.cpp)**

```cpp
#include "tcApp.h"
#include "tcImGui.h" // Addon

void tcApp::setup() {
    // Set loop mode
    tc::setLoopMode(tc::LoopMode::Game);

    // Since App is also a Node, timers and events work
    this->callAfter(1.0, []{ printf("1sec passed!\n"); });

    tc::Gui::setup();
    image = std::make_shared<tc::Texture>("test.png");
}

void tcApp::update() {
    // Node update processing is called by base class, no need to write
    // Only write your own logic here
}

void tcApp::draw() {
    tc::clear(0.1f, 0.1f, 0.1f);

    // Addon
    tc::Gui::begin();
    ImGui::SliderFloat("Angle", &angle, 0, 360);
    tc::Gui::end();

    tc::pushMatrix();
    tc::translate(640, 360);
    tc::rotate(angle);
    image->draw(-50, -50, 100, 100);
    tc::popMatrix();
}
```

### **3. Entry Point (main.cpp)**

Explicit style of creating a settings object and launching the app.

```cpp
#include "tcApp.h"

int main() {
    // 1. Create settings object (oF-like)
    tc::WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("My TrussC App");
    // settings.setFullscreen(true);
    // settings.setHighDpi(false);
    // settings.setSampleCount(8);  // MSAA 8x

    // 2. Specify app and run
    return tc::runApp<tcApp>(settings);
}
```

### **4. Global Helper Functions**

oF-style global functions callable from anywhere.

```cpp
namespace tc {
    // ----- Window Control -----
    void setWindowTitle(const std::string& title);
    void setWindowSize(int w, int h);
    void setWindowPos(int x, int y);
    void setFullscreen(bool full);
    bool isFullscreen();
    int getWindowWidth();   // Window width
    int getWindowHeight();  // Window height

    // ----- Screen Info -----
    int getScreenWidth();   // Display width
    int getScreenHeight();  // Display height

    // ----- Mouse (Window Coordinates) -----
    float getMouseX();      // Mouse X in window
    float getMouseY();      // Mouse Y in window
    float getPMouseX();     // Previous frame mouse X
    float getPMouseY();     // Previous frame mouse Y
    bool isMousePressed();
    int getMouseButton();

    // ----- Time -----
    double getElapsedTime();   // Seconds since app start
    double getDeltaTime();     // Seconds since previous frame
    uint64_t getFrameCount();  // Frame number

    // ----- Loop Control -----
    void setLoopMode(LoopMode mode);  // Game or Demand
    void redraw();  // Request redraw in Demand mode
}
```

**Coordinate System Concept:**
* `tc::getMouseX()` / `tc::getMouseY()`: Coordinates with window top-left as origin
* `tc::Node::onMousePress(localX, localY)`: Coordinates transformed to that node's local coordinate system

## **5. Core Architecture**

### **A. Loop Modes (On-Demand Rendering)**

Switch main loop behavior based on application purpose.

* **tc::LoopMode::Game (Default):**
  * Calls update / draw every frame synced to VSync. For animated works.
* **tc::LoopMode::Demand (Tool / Eco):**
  * Draws only when input events occur or tc::redraw() is explicitly called.
  * For tool apps or battery-conscious resident apps.

### **B. Scene Graph & Event System**

Incorporates ofxComponent philosophy into core, enabling relative coordinate management and advanced event control.

* **tc::Node Class:**
  * Base class with parent-child relationships and local transformation matrix.
  * Has interfaces like virtual void onMousePress(...), but **not called by default**.
  * Only nodes that call this->enableEvents() become hit test targets (performance optimization).
* **Activation Control (isActive):**
  * When isActive is false, that node and all descendants **completely stop**.
  * No update, draw, event detection, or timer progression (equivalent to Unity's SetActive(false)).
* **Visibility Control (isVisible):**
  * When isVisible is false, **only draw is skipped**.
  * Update and event detection continue, enabling "invisible hit areas" or "hidden background processing".
* **Orphan Nodes (parentless nodes):**
  * Nodes just created or after removeChild, not in scene graph (tree rooted at tc::App).
  * **Behavior:** Exists in memory but no automatic update, draw, or event processing (dormant state).
  * **Manual control:** Works if user explicitly calls node->updateTree() or node->draw(), but addChild is recommended.
* **Event Traverse Rule (Skipper Type):**
  * Even if parent node has enableEvents() disabled, **child node exploration continues**.
  * This allows buttons under "grouping nodes" without events to respond correctly.
* **Hit Test Logic:**
  * virtual bool hitTest(const tc::Ray& localRay)
  * Each node performs hit detection in its own local coordinate system.
  * Mouse coordinates are treated as Ray for 2D/3D mixed support, using plane or shape intersection.
* **Event Consumption (Blocking):**
  * Event handlers return bool. true consumes the event (stops propagation to others).
* **Global Event Listeners:**
  * Mechanism to hook screen-wide events independent of Node tree.
  * Based on std::function, making lambda and member function binding easy.

### **C. Timer System (Synchronous)**

Inherits and extends ofxComponent's addTimerFunction, providing safe delayed execution on main thread.

* **Timer Structure:**
  * Managed with lightweight structs.
  * std::function<void()> callback: Processing to execute.
  * double targetTime: Scheduled execution time (based on elapsed time since app start).
  * double interval: Repeat interval (0.0 for one-shot).
  * bool isDead: Deletion flag (becomes true after execution or cancellation, collected in Sweep phase).
  * uint64_t id: Identifier for canceling specific timers. Uses 64-bit integer to prevent practical exhaustion.
* **Phase Control (Pre/Post Update):**
  * **Pre-Update:** Normal timers (delay > 0) are processed **before update()** execution. This updates state before entering update() if scheduled time has passed at frame start.
  * **Post-Update:** Immediate timers (delay == 0) are processed **right after update()** execution. Exceptional but allows immediate reflection within current frame.
* **Lifecycle Binding:**
  * Timers are bound to tc::Node instances. Auto-destroyed when Node is deleted.
* **Zero Overhead:**
  * When timer list is empty, check processing returns immediately, so CPU load for nodes not using timers is nearly zero.

### **D. Safe State Mutation (Lifecycle Safety)**

Adopts deferred application pattern to prevent crashes from structural changes during iteration (Invalidation).

* **Node Hierarchy:**
  * addChild / removeChild called during update() loop are not applied immediately but accumulated in **Pending Queue**.
  * Batch applied in tc::App's frame end processing (resolveStructure()), guaranteeing iterator safety.
* **Timer Mutation:**
  * When new timers are added inside timer callbacks, they're added to pendingTimers list and activated from next frame.
  * When clear() is called during execution, only isDead flag is set on target timer, with memory deletion after loop ends (Sweep).

### **E. Rendering & Clipping**

Lightweight drawing restrictions without FBO, and intuitive state management.

* **State Management (Pipeline Cache):**
  * Provides APIs like tc::setBlendMode(mode), tc::enableDepthTest().
  * Internally uses Sokol's pipeline object (PSO) caching mechanism to minimize state change overhead.
* **Blend Strategy (Separate Blend):**
  * Uses Sokol's separate_blend feature to set RGB and Alpha formulas separately.
  * **RGB:** Src * SrcA + Dst * (1 - SrcA) (normal)
  * **Alpha:** Src * 1 + Dst * (1 - SrcA) (preserve/add)
  * This guarantees correct behavior where opaque backgrounds (FBO, etc.) maintain opacity when drawing semi-transparent objects (colors don't "bleed through").
* **Scissor Clipping:**
  * tc::Node has isClipping flag and width, height.
  * When enabled, sg_apply_scissor_rect is applied on draw() call, then parent's Scissor setting is restored after drawing.
  * Rectangles are always axis-aligned (AABB), no rotation support (UI-focused, performance priority).
  * Event system (HitTest) also respects this clipping range, blocking mouse events outside the range.

### **F. Graphics Context (Stack System)**

Follows oF's push/pop pattern while supporting modern RAII style.

* **Matrix Stack:**
  * tc::pushMatrix() / tc::popMatrix() save/restore coordinate transformation matrices.
  * Also used internally during tc::Node drawing.
* **Style Stack:**
  * tc::pushStyle() / tc::popStyle() batch save/restore drawing state.
  * **Saved items:**
    * Color (Fill / Stroke)
    * **Blend Mode** (important: including this guarantees intuitive behavior)
    * Line Width
    * Rect Mode / Image Mode
    * Depth Test / Culling / Scissor State
* **Scoped Objects (RAII):**
  * Provides the following scope objects to prevent forgetting manual pop.
  * tc::ScopedMatrix: push on constructor, pop on destructor.
  * tc::ScopedStyle: General style save/restore.
  * tc::ScopedBlendMode: Temporarily change only blend mode (lightweight).

### **G. Threading & Concurrency**

Based on modern C++ standard features while providing convenience needed for creative coding.

* **Mutex:**
  * No custom wrapper class provided.
  * Recommend using standard std::mutex and std::lock_guard (RAII). These provide safe mutual exclusion with same feel as tc::ScopedStyle.
* **Thread:**
  * Provides tc::Thread class.
  * Functions as std::thread wrapper, simplifying thread lifecycle management (start, stop, join).
  * Adopts inheritance style implementing threadedFunction(), making oF user migration easy.

## **6. Critical Implementation: Native Wrapper Strategy**

To prevent external library dependencies (GPL contamination, size bloat), the following features are implemented by wrapping OS-specific APIs. Area to maximize AI coding utilization.

### **A. Video & Camera (No FFmpeg)**

* **macOS (AVFoundation):**
  * Coordinate CoreVideo and Metal (CVMetalTextureCache) for **zero-copy** texturing.
* **Windows (Media Foundation):**
  * Use IMFSourceReader to bind directly as Direct3D 11 texture.

### **B. Serial (No Boost / Legacy Libraries)**

* **Implementation:**
  * POSIX (Mac/Linux): Implement with open(), tcsetattr(), read().
  * Windows: Implement with CreateFile(), SetCommState(), ReadFile().

## **7. Class Design Policy**

Clearly distinguish between classes emphasizing extensibility and classes emphasizing performance.

### **A. Infrastructure Objects**

Policy: Actively use virtual, encourage user inheritance/extension.
Memory Management: std::shared_ptr automatic management as basis.

* **Targets:** tc::Node, tc::App, tc::Window, tc::Serial, tc::VideoPlayer, tc::Font, etc.
* **Design:**
  * **Creation:** Recommend std::make_shared<T>().
  * **Ownership:** Parent node owns children via std::vector<std::shared_ptr<Node>>.
  * tc::Node has internal method updateTree() to control execution order.
  * All tc::Node event handlers are virtual. However, execution controlled by enableEvents() flag.
  * Internal state variables are protected, or exposed through protected accessors.

### **B. Data Objects**

Policy: Prioritize performance and memory layout (POD-ness), discourage inheritance.
Memory Management: Basically value semantics, or shared_ptr as needed.

* **Targets:** tc::Vec3, tc::Color, tc::Matrix, tc::Mesh, tc::Texture, etc.
* **Design:**
  * No virtual methods.
  * Structs transferred to GPU maintain standard layout.

## **8. Roadmap to MVP**

Goals for "Ver 0.1" to aim for first.

1. **Base:** Window display and screen clear with sokol_app + sokol_gfx.
2. **Scene:** Implement tc::Node and event system (reverse traversal, Ray-based HitTest, timers).
3. **Wrapper:** Implement basic drawing like tc::drawRect and tc::RectNode (with 2D hit detection).
4. **Modules:** Implement tc::Gui (ImGui) as official module.
5. **Build:** Complete CMakeLists.txt template that works from any directory.
