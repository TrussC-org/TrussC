# TrussC Project Settings

## Overview
TrussC is a lightweight creative coding framework based on sokol.
It aims for an API similar to openFrameworks while implementing simply in modern C++.

## Folder Structure

```
trussc/                           # TrussC core library
├── CMakeLists.txt                # Main CMake (GLOB-based)
├── include/
│   ├── TrussC.h                  # Entry point (includes everything)
│   ├── impl/                     # Library implementations (stb, pugixml, etc.)
│   ├── tc/
│   │   ├── app/                  # Application foundation
│   │   ├── graphics/             # Drawing related
│   │   ├── math/                 # Math utilities
│   │   ├── types/                # Basic types (Color, Node, etc.)
│   │   ├── events/               # Event system
│   │   ├── utils/                # Utilities
│   │   ├── 3d/                   # 3D features
│   │   ├── gl/                   # Low-level graphics
│   │   ├── sound/                # Sound (.h + .cpp)
│   │   ├── network/              # Network (.h + .cpp)
│   │   └── video/                # Video
│   ├── sokol/                    # sokol headers
│   ├── imgui/                    # Dear ImGui
│   ├── stb/                      # stb libraries
│   └── ...
├── platform/                     # Platform-specific implementations
│   ├── mac/                      # macOS (.mm)
│   ├── win/                      # Windows (.cpp)
│   └── linux/                    # Linux (.cpp)
├── cmake/                        # CMake helpers
│   ├── trussc_app.cmake          # App configuration macros
│   └── use_addon.cmake           # Addon loading macros
└── resources/                    # Resources (default icons, etc.)

addons/                           # Optional addons (same level as trussc)
├── tcxTls/                       # TLS/SSL support (mbedTLS, FetchContent)
│   ├── CMakeLists.txt
│   ├── src/                      # Headers + source
│   └── example-tls/              # Sample
├── tcxBox2d/                     # Box2D physics engine (FetchContent)
│   ├── CMakeLists.txt
│   ├── src/
│   ├── example-basic/
│   └── example-node/
└── tcxOsc/                       # OSC protocol
    ├── src/
    └── example-osc/

examples/                         # Sample projects
├── templates/                    # Templates
├── graphics/                     # Drawing samples
├── 3d/                           # 3D samples
├── network/                      # Network samples
├── tools/
│   └── projectGenerator/         # Project generation tool
└── ...

projectGenerator/                 # Build script distribution
├── buildProjectGenerator_mac.command
├── buildProjectGenerator_win.bat
└── buildProjectGenerator_linux.sh
```

## Naming Conventions

- File names: `tcXxx.h` (lowercase tc + PascalCase)
- Namespace: `trussc::` (alias `tc::`)
- Classes/Structs: PascalCase (e.g., `Color`, `WindowSettings`)
- Functions: camelCase (e.g., `drawRect`, `setColor`)
- Constants: SCREAMING_SNAKE_CASE or camelCase (math constants: `TAU`, `PI`, etc.)
- Color constants: `tc::colors::camelCase` (e.g., `tc::colors::cornflowerBlue`)

## Coding Style

- Uses C++20
- Header-only (inline functions) as default
- Wrap sokol API for ease of use
- Aim for API similar to openFrameworks
- Omit `std::` prefix (assuming using namespace std)

## Build

```bash
# Build a sample
cd examples/graphics/colorExample
mkdir build && cd build
cmake ..
cmake --build .

# Build projectGenerator (macOS)
./projectGenerator/buildProjectGenerator_mac.command
```

## Related Documentation

- [docs/GET_STARTED.md](docs/GET_STARTED.md) - Getting started (read this first)
- [docs/TrussC_vs_openFrameworks.md](docs/TrussC_vs_openFrameworks.md) - API comparison for oF users
- [docs/HOW_TO_BUILD.md](docs/HOW_TO_BUILD.md) - Detailed build instructions, icon settings, distribution
- [docs/ADDONS.md](docs/ADDONS.md) - How to use addons
- [docs/DESIGN.md](docs/DESIGN.md) - Design details (Loop Architecture, 3D Projection, etc.)
- [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) - Concept, philosophy, tech stack
- [docs/ROADMAP.md](docs/ROADMAP.md) - Implementation roadmap

## Language Policy

All code comments and commit messages in this project should be written in **English**.
This ensures consistency and accessibility for international contributors.

## References

- openFrameworks: https://openframeworks.cc/
- sokol: https://github.com/floooh/sokol
