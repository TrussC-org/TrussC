# TrussC

[![Build](https://github.com/TrussC-org/TrussC/actions/workflows/build.yml/badge.svg)](https://github.com/TrussC-org/TrussC/actions/workflows/build.yml)

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
cmake --preset macos      # or: windows, linux, web
cmake --build build-macos

# Run (macOS)
./bin/graphicsExample.app/Contents/MacOS/graphicsExample
```

### Minimal Code

```cpp
#include "TrussC.h"
using namespace std;
using namespace tc;

class MyApp : public App {
    void draw() override {
        clear(0.1);  // Clear to dark gray

        setColor(1.0, 0.4, 0.4);
        drawCircle(getWindowWidth() / 2, getWindowHeight() / 2, 100);
    }
};

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("My App");
    return runApp<MyApp>(settings);
}
```

## Examples

| Category | Examples |
|----------|----------|
| **templates/** | emptyExample |
| **graphics/** | graphicsExample, colorExample, fontExample, shaderExample, fboExample |
| **3d/** | 3DPrimitivesExample, easyCamExample |
| **node/** | nodeExample, hitTestExample, uiExample |
| **input_output/** | keyboardExample, mouseExample, imageLoaderExample, screenshotExample |
| **sound/** | soundPlayerExample, micInputExample, chipSoundExample |
| **video/** | videoPlayerExample, videoGrabberExample |
| **network/** | tcpExample, udpExample |
| **ImGui/** | imguiExample |
| **utils/** | timerExample, clipboardExample |
| **tools/** | projectGenerator |

## Main API

### Drawing

```cpp
clear(0.1);                      // Clear background (0-1)
setColor(1.0, 0.5, 0.0);         // Set draw color (0-1)
drawRect(x, y, w, h);            // Rectangle
drawCircle(x, y, radius);        // Circle
drawLine(x1, y1, x2, y2);        // Line
drawTriangle(x1, y1, ...);       // Triangle
```

### Transform

```cpp
translate(x, y);
rotate(radians);
scale(sx, sy);
pushMatrix();
popMatrix();
```

### Input

```cpp
getMouseX();
getMouseY();
isMousePressed();
```

### Time

```cpp
getElapsedTime();    // Seconds since start
getDeltaTime();      // Seconds since last frame
getFrameRate();      // FPS
```

### Color

```cpp
Color c(1.0, 0.5, 0.0);              // RGB (0-1)
Color::fromHSB(hue, sat, bri);       // From HSB
colors::cornflowerBlue;              // Predefined colors
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
trussc/include/
├── TrussC.h          # Main header (include this)
├── tcBaseApp.h       # App base class
├── tc/               # Feature headers
│   ├── graphics/     # Shapes, images, fonts
│   ├── gpu/          # FBO, shaders, textures
│   ├── 3d/           # 3D transforms, primitives, camera
│   ├── types/        # Color, Vec2/3/4, etc.
│   ├── events/       # Event system
│   ├── math/         # Math utilities, noise
│   ├── sound/        # Audio playback
│   ├── video/        # Video playback, webcam
│   ├── network/      # TCP, UDP
│   ├── utils/        # Timer, file dialogs, etc.
│   └── gui/          # Dear ImGui integration
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
