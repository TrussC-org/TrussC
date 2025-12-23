# TrussC

A lightweight creative coding framework based on sokol.
Inspired by openFrameworks, implemented simply with modern C++.

## Features

- **Lightweight**: Minimal dependencies, built on sokol
- **Header-only**: Most features are header-only
- **C++20**: Leverages modern C++ features
- **Cross-platform**: macOS (Metal), Windows (D3D11), Web (WebGPU). Linux/Raspberry Pi planned.
- **oF-like API**: Familiar design for openFrameworks users

## Quick Start

**-> See [GET_STARTED.md](docs/GET_STARTED.md) to get up and running!**

### Using Project Generator (Recommended)

Use `examples/tools/projectGenerator` to create projects via GUI.
Supports VSCode, Cursor, Xcode, and Visual Studio.

### Command Line Build

```bash
# Build an example
cd examples/graphics/graphicsExample
mkdir build && cd build
cmake ..
cmake --build .

# Run (macOS)
./bin/graphicsExample.app/Contents/MacOS/graphicsExample
```

### Minimal Code

```cpp
#include "TrussC.h"
#include "tcBaseApp.h"

class MyApp : public tc::App {
public:
    void setup() override {
        // Initialization
    }

    void draw() override {
        tc::clear(30);  // Clear background to gray

        tc::setColor(255, 100, 100);
        tc::drawCircle(400, 300, 100);  // Draw a red circle
    }
};

int main() {
    tc::runApp<MyApp>({
        .width = 800,
        .height = 600,
        .title = "My App"
    });
    return 0;
}
```

## Examples

### templates/
- **emptyExample** - Minimal project template

### graphics/
- **graphicsExample** - Basic shape drawing
- **colorExample** - Color spaces and interpolation

### 3d/
- **ofNodeExample** - Scene graph / node system
- **3DPrimitivesExample** - 3D primitives
- **easyCamExample** - Mouse-controlled 3D camera

### math/
- **vectorMathExample** - Vector and matrix operations

### input_output/
- **imageLoaderExample** - Image loading and drawing
- **screenshotExample** - Screenshot using FBO

### events/
- **keyPressedExample** - Keyboard events
- **mouseExample** - Mouse events
- **allEventsExample** - All events overview

### threads/
- **threadExample** - Multithreading

## Main API

### Drawing

```cpp
tc::clear(r, g, b);              // Clear background
tc::setColor(r, g, b, a);        // Set draw color
tc::drawRect(x, y, w, h);        // Rectangle
tc::drawCircle(x, y, radius);    // Circle
tc::drawLine(x1, y1, x2, y2);    // Line
tc::drawTriangle(x1, y1, ...);   // Triangle
```

### Transform

```cpp
tc::translate(x, y);
tc::rotate(radians);
tc::scale(sx, sy);
tc::pushMatrix();
tc::popMatrix();
```

### Input

```cpp
tc::getMouseX();
tc::getMouseY();
tc::isMousePressed();
```

### Time

```cpp
tc::getElapsedTime();    // Seconds since start
tc::getDeltaTime();      // Seconds since last frame
tc::getFrameRate();      // FPS
```

### Color

```cpp
tc::Color c(1.0f, 0.5f, 0.0f);           // RGB
tc::colorFromHSB(hue, sat, brightness);  // From HSB
tc::colors::cornflowerBlue;              // Predefined colors
```

## Why TAU?

TrussC uses TAU (τ = 2π) as the primary circle constant instead of PI.

```cpp
// TAU = one full rotation
rotate(TAU * 0.25f);   // Quarter turn (90°)
rotate(TAU * 0.5f);    // Half turn (180°)
rotate(TAU);           // Full turn (360°)

// PI is deprecated (will emit compiler warning)
rotate(PI);            // Warning: Use TAU instead
```

**Why?** TAU maps directly to the unit circle:
- `TAU * 0.25` = quarter turn = 90°
- `TAU * 0.5` = half turn = 180°
- `TAU * 0.75` = three-quarter turn = 270°
- `TAU` = full turn = 360°

With PI, you constantly multiply by 2 for full rotations. TAU eliminates this mental overhead.

PI is still available for compatibility, but marked `[[deprecated]]` to encourage TAU adoption.

See also: [The Tau Manifesto](https://tauday.com/tau-manifesto)

## Dependencies

sokol, Dear ImGui, stb, miniaudio, etc. are all bundled in `trussc/include/`.
See [LICENSE.md](docs/LICENSE.md) for details.

## Directory Structure

```
include/
├── TrussC.h          # Main header (include this)
├── tcBaseApp.h       # App base class
├── tc/               # Feature headers
│   ├── graphics/     # Drawing
│   ├── math/         # Math utilities
│   ├── types/        # Color, Vec2, etc.
│   ├── events/       # Event system
│   ├── 3d/           # 3D features
│   └── gl/           # FBO, shaders, etc.
├── sokol/            # sokol library
└── stb/              # stb library

examples/             # Example projects
src/                  # Platform-specific implementations
```

## Documentation

- [GET_STARTED.md](docs/GET_STARTED.md) - Getting started guide
- [TrussC_vs_openFrameworks.md](docs/TrussC_vs_openFrameworks.md) - API mapping for oF users
- [ADDONS.md](docs/ADDONS.md) - How to use addons
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) - Design philosophy and architecture
- [BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md) - CMake build system details
- [ROADMAP.md](docs/ROADMAP.md) - Development roadmap

## License

MIT License - See [LICENSE.md](docs/LICENSE.md) for details.

## References

- [openFrameworks](https://openframeworks.cc/)
- [sokol](https://github.com/floooh/sokol)
