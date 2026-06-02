# TrussC Addons

TrussC can be extended with an addon system, similar to openFrameworks' ofxAddon.

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Using Addons](#using-addons)
3. [Installing Existing Addons](#installing-existing-addons)
4. [Creating Addons](#creating-addons)
5. [Naming Conventions](#naming-conventions)
6. [Dependencies](#dependencies)
7. [addon.json Specification](#addonjson-specification)
8. [Registry & Distribution](#registry--distribution)
9. [Bundled Addon Details](#bundled-addon-details)

---

## Design Philosophy

TrussC project structure is based on **"CMakeLists.txt is shared across all projects, users only edit addons.make"**.

### Why This Design

- **Simple**: Only one project-specific file to edit: `addons.make`
- **Easy maintenance**: No need to modify CMakeLists.txt
- **Hard to get wrong**: Clear what file to edit

### Files Users Edit

| File | Purpose | When to Edit |
|------|---------|--------------|
| `addons.make` | Specify addons to use | When adding/removing addons |
| `local.cmake` | Project-specific CMake config (optional) | When linking system libraries or adding project-specific build settings |
| `src/*.cpp` | Application code | Always |

### Files Users Don't Edit

| File | Reason |
|------|--------|
| `CMakeLists.txt` | Shared across all projects. `trussc_app()` handles everything automatically |

### Addons vs `local.cmake`

Not every dependency needs to be an addon. If a library is only used by one project, use `local.cmake` instead:

```cmake
# local.cmake - project-specific dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(EXIV2 REQUIRED exiv2)
target_include_directories(${PROJECT_NAME} PRIVATE ${EXIV2_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${EXIV2_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${EXIV2_LIBRARIES})
```

**Make it an addon** when the library needs a reusable wrapper (classes, helper functions) or when multiple projects will use it. **Use `local.cmake`** when you just need to link a system library with no wrapper code.

See [BUILD_SYSTEM.md](BUILD_SYSTEM.md#6-project-local-cmake-config-localcmake) for details.

---

## Using Addons

### 1. Create addons.make

Create an `addons.make` file in your project folder, listing addon names one per line:

```
tcxOsc
tcxBox2d
```

Comments are also supported:

```
# Physics engine
tcxBox2d

# Networking (add later)
# tcxOsc
```

### 2. Use in Code

```cpp
#include <tcxBox2d.h>

using namespace tcx::box2d;

World world;
Circle circle;

void setup() {
    world.setup(Vec2(0, 300));
    circle.setup(world, 400, 100, 30);
}
```

### CMakeLists.txt (Reference)

CMakeLists.txt is shared across all projects. No editing required:

```cmake
cmake_minimum_required(VERSION 3.20)

set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

trussc_app()
```

`trussc_app()` automatically:

- Gets project name from folder name
- Collects and builds `src/*.cpp`
- Links TrussC
- Adds addons from `addons.make`
- Configures macOS bundle, icons, etc.

---

## Installing Existing Addons

### Bundled Addons

Addons included with TrussC:

| Addon | Description |
|-------|-------------|
| tcxBox2d | 2D physics engine (Box2D) |
| tcxCurl | HTTPS client (libcurl) |
| tcxGltf | glTF 2.0 / GLB model loader (cgltf) |
| tcxHap | Hap video codec for fast GPU playback |
| tcxImGui | Dear ImGui integration |
| tcxLut | 3D LUT color grading |
| tcxObj | Wavefront OBJ model import/export |
| tcxOsc | OSC (Open Sound Control) protocol |
| tcxQuadWarp | Quad warp / projection mapping |
| tcxTls | TLS/SSL communication (mbedTLS) |
| tcxWebSocket | WebSocket client and server |

### Third-Party Addons

1. Place addon in `addons/` folder
2. Add to `addons.make`

```
tc_v0.0.1/
└── addons/
    ├── tcxBox2d/        # Official
    ├── tcxOsc/          # Official
    └── tcxSomeAddon/    # Third-party
```

---

## Creating Addons

### Folder Structure

```
addons/tcxMyAddon/
├── src/                     # Addon code (.h + .cpp together)
│   ├── tcxMyAddon.h         # Main header (includes everything)
│   ├── tcxMyClass.h
│   └── tcxMyClass.cpp
├── libs/                    # External libraries (git submodule, etc.)
│   └── somelib/
├── example-basic/           # Example (same level as src, example-xxx format)
│   ├── src/
│   │   ├── main.cpp
│   │   └── tcApp.cpp
│   ├── addons.make          # Addons used by this example
│   └── CMakeLists.txt       # Shared template
└── CMakeLists.txt           # Optional (only for FetchContent, etc.)
```

**Key Points:**
- `src/`: Addon's own code. Keep `.h` and `.cpp` in the same place
- `libs/`: External source code, git submodules, etc.
- `example-xxx/`: Examples at same level as `src/`. CMakeLists.txt uses shared template
- `CMakeLists.txt`: Usually not needed. Create only for special processing like FetchContent

### When CMakeLists.txt Is Not Needed

Addons meeting these conditions don't need CMakeLists.txt:

- Complete with just `src/` sources
- No external libraries, or sources included in `libs/`
- No dependencies other than TrussC

### When CMakeLists.txt Is Needed

When fetching external libraries with FetchContent:

```cmake
# tcxBox2d/CMakeLists.txt

set(ADDON_NAME tcxBox2d)

# Get Box2D via FetchContent
include(FetchContent)
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v2.4.1
)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# Source files
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})
target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${ADDON_NAME} PUBLIC box2d TrussC)
```

### Shipping a runtime file with the app (`tc_addon_bundle_file`)

When an addon needs a file beside the final executable at **runtime** — a
Windows sensor-SDK `.dll`, a macOS `.dylib`/framework or `.metallib`, a data
blob — register it from the addon's `CMakeLists.txt`:

```cmake
# tc_addon_bundle_file(<path> [MACOS_DEST <subdir>])

# Windows DLL beside the .exe:
if(WIN32)
    tc_addon_bundle_file("${CMAKE_CURRENT_SOURCE_DIR}/libs/sensor/sensor.dll")
endif()

# Generated Metal library into the app bundle's Resources (see tcxSyphon):
tc_addon_bundle_file("${CMAKE_CURRENT_BINARY_DIR}/default.metallib" MACOS_DEST Resources)
```

TrussC copies the file into whatever app consumes the addon — `.app/Contents/<MACOS_DEST>`
(default `Resources`) on macOS, next to the executable on Windows/Linux — with
no need to know the app target's name. For generated files, `add_dependencies()`
your addon target on the generator so the file exists before the copy runs.

### Main Header (tcxMyAddon.h)

```cpp
// =============================================================================
// tcxMyAddon - Addon description
// =============================================================================

#pragma once

// Include all headers
#include "tcxMyClass.h"
#include "tcxAnotherClass.h"
```

### Class Implementation Example

```cpp
// tcxMyClass.h
#pragma once

#include <TrussC.h>

namespace tcx::myaddon {

class MyClass {
public:
    void setup();
    void update();
    void draw();

private:
    // ...
};

} // namespace tcx::myaddon
```

---

## Naming Conventions

### Addon Names

| Item | Convention | Example |
|------|------------|---------|
| Folder name | `tcx` + PascalCase | `tcxBox2d`, `tcxGui` |
| Library name | Same as folder name | `tcxBox2d` |
| Main header | `AddonName.h` | `tcxBox2d.h` |

### Namespaces

```cpp
namespace tcx::addonname {
    // ...
}
```

| Addon | Namespace |
|-------|-----------|
| tcxBox2d | `tcx::box2d` |
| tcxGui | `tcx::gui` |
| tcxOsc | `tcx` (classes are `Osc`-prefixed: `OscSender`, `OscMessage`, …) |

**Note:** TrussC core uses `tc::`. Addons use `tcx::`.

### File Names

| Type | Convention | Example |
|------|------------|---------|
| Header | `tcx` + PascalCase + `.h` | `tcxBox2dWorld.h` |
| Implementation | `tcx` + PascalCase + `.cpp` | `tcxBox2dWorld.cpp` |

### Class Names

No prefix within namespace:

```cpp
namespace tcx::box2d {
    class World { ... };    // OK: tcx::box2d::World
    class Circle { ... };   // OK: tcx::box2d::Circle
}
```

---

## Dependencies

### Depending on Other Addons

Use `use_addon()` in the addon's CMakeLists.txt:

```cmake
# tcxMyAddon depends on tcxBox2d
add_library(${ADDON_NAME} STATIC ...)

use_addon(${ADDON_NAME} tcxBox2d)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
```

When users add `tcxMyAddon` to `addons.make`, tcxBox2d is automatically included too.

### Depending on External Libraries

Use FetchContent:

```cmake
include(FetchContent)

FetchContent_Declare(
    somelib
    GIT_REPOSITORY https://github.com/example/somelib.git
    GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(somelib)

target_link_libraries(${ADDON_NAME} PUBLIC somelib)
```

---

## addon.json Specification

Every addon repository should contain an `addon.json` file at the root. The file declares metadata used by the registry crawler, the TrussC website, and `trusscli`.

All fields are optional — an empty `{}` is valid and signals "this is a TrussC addon". Filling in fields like `license` and `description` improves how the addon appears in listings.

### Example

```jsonc
{
  "name":           "tcxFoo",
  "description":    "Short one-line summary",
  "version":        "0.1.0",
  "author":         "Your Name <you@example.com>",
  "license":        "MIT",
  "trussc_version": ">=0.5.0",
  "category":       "physics",
  "screenshot":     "docs/preview.png",
  "demo_url":       "https://trussc.org/sketch/?example=tcxFoo",
  "platforms":      ["macos", "win", "linux", "web"],
  "keywords":       ["2d", "collision"],

  "dependencies": [
    "tcxCurl",
    { "name": "tcxBox2d", "tag":    "v0.2.0" },
    { "name": "tcxOsc",   "branch": "main" },
    { "name": "tcxBaz",   "commit": "a1b2c3d" },
    { "name": "tcxBar",   "url":    "https://github.com/user/tcxBar" }
  ]
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Addon name. Should match the repository name. If omitted, the registry uses the repo name. |
| `description` | string | One-line summary. Used in registry listings and `trusscli addon list`. |
| `version` | string | Self-reported version (e.g., `"0.1.0"`). If empty, the crawler falls back to the latest git tag. |
| `author` | string | Author name, optionally with email (`"Name <email>"`). |
| `license` | string | SPDX identifier (e.g., `"MIT"`, `"GPL-3.0"`). Missing values display as `"Unknown"`. |
| `trussc_version` | string | TrussC version range this addon targets (e.g., `">=0.5.0"`). |
| `category` | string | Top-level grouping. See [Categories](#categories). Unknown or omitted values fall back to `"misc"`. |
| `screenshot` | string | Relative path or absolute URL to a preview image. Relative paths are resolved against the repo's default branch. |
| `demo_url` | string | Link to a live demo (e.g., a TrussSketch page or a web build). |
| `platforms` | string[] | Supported platforms. Allowed values: `"macos"`, `"win"`, `"linux"`, `"web"`, `"android"`, `"ios"`. Omit the field if untested — empty array means "no supported platforms". |
| `keywords` | string[] | Free-form tags for fine-grained filtering inside a category (e.g., a `graphics` addon may use `"shader"`, `"glitch"`, `"color-grading"`). |
| `dependencies` | (string \| object)[] | Other addons this addon depends on. See below. |

### Categories

`category` is a single string from the fixed set below. The set is intentionally small so addons stay easy to browse; use `keywords` for sub-classification within a category (e.g., `category: "graphics"` + `keywords: ["shader", "post-fx"]`).

| Value | Covers |
|-------|--------|
| `3d` | 3D models, geometry, mesh utilities, scene graph |
| `ai` | LLM clients, generative AI, prompt tooling |
| `algorithms` | General algorithms (sorting, graph, search, etc.) |
| `animation` | Keyframes, tweening, easing, motion |
| `bridges` | Other-language/runtime interop (Lua, Julia, Python) |
| `computer-vision` | OpenCV wrappers, marker / face / pose detection |
| `game` | Game-specific systems (ECS, entity AI) |
| `graphics` | Drawing, shaders, post-effects, color grading, image filters / format I/O |
| `gui` | UI widgets, IME, ImGui-style integration |
| `hardware` | Arduino, MIDI, sensors, IMU, embedded I/O |
| `machine-learning` | Classical ML, on-device inference engines |
| `network` | TCP/UDP/OSC/MQTT/NDI and other transport-layer protocols |
| `physics` | Rigid body, soft body, fluid simulation |
| `sound` | Audio synthesis, playback, DSP |
| `typography` | Font loading, text shaping/rendering |
| `utilities` | File I/O, settings, logging, generic helpers |
| `video` | Video playback, capture, codecs |
| `web` | HTTP, WebView, JS bridge (Emscripten), OAuth flows |
| `misc` | Fallback only — the crawler maps unknown / omitted categories here. Do not declare `"misc"` explicitly. |

Pick the single best fit. If two seem equally valid, prefer the category that matches the addon's primary functionality (what users would search for first).

### Dependencies

Each entry is either a plain string (addon name only, resolved through the registry at its default branch) or an object with optional pinning:

```jsonc
{ "name": "tcxBox2d", "tag":    "v0.2.0" }   // pinned to a tag
{ "name": "tcxOsc",   "branch": "main"   }   // pinned to a branch
{ "name": "tcxFoo",   "commit": "a1b2c3d" }  // pinned to a commit
{ "name": "tcxBar",   "url":    "https://github.com/user/tcxBar" }  // off-registry
```

Rules:

- `tag`, `branch`, and `commit` are mutually exclusive. If more than one is set, `commit` wins (with a warning).
- `url` is optional; it lets you depend on addons that are not in the registry (including non-GitHub hosts).
- When `trusscli addon clone` resolves dependencies, it walks the graph recursively. Circular dependencies are detected and aborted.

### Notes on Optional Fields

- An addon.json with only `{}` is enough to be picked up by the crawler. Filling in `license`, `description`, and `screenshot` is strongly recommended for discoverability.
- `name` is informational only — the registry always uses the repository name as the canonical identifier.

---

## Registry & Distribution

Addons are discovered automatically through GitHub topics. The official registry lives at:

**Repository:** [`TrussC-org/trussc-addons`](https://github.com/TrussC-org/trussc-addons)
**Registry JSON:** Published on the `gh-pages` branch via GitHub Pages.

### Discovery Conditions

A repository is included in the registry when **all** of the following are true:

1. It has the GitHub topic `trussc-addon`.
2. Its name matches `tcx[A-Z]...` (e.g., `tcxFoo`).
3. It contains an `addon.json` file at the root (empty `{}` is acceptable).
4. It is not `private` and not `archive`d.

GitHub topics are **not** inherited by forks, so forks only appear in the registry if the forker explicitly adds the topic. This means the upstream original is referenced by default, and intentional forks can coexist.

### Crawler

A GitHub Actions workflow on `trussc-addons` runs on a daily cron (and on demand) to:

1. Search GitHub for `topic:trussc-addon`.
2. Filter results by the conditions above.
3. Fetch each repo's `addon.json` (and the latest git tag as a `version` fallback).
4. Emit `registry.json` and publish it to `gh-pages`.

The published `registry.json` is consumed by:

- `trusscli` (`addon list`, `addon search`, `addon clone`)
- The TrussC website
- Third-party tools

> **Note on rate limits:** Direct topic searches by every user would hit GitHub API rate limits. Using a precomputed `registry.json` over GitHub Pages gives everyone a free, fast endpoint.

### Registry Entry Schema

Each addon entry in `registry.json` looks like:

```jsonc
{
  "name": "tcxLua",
  "owner": "funatsufumiya",            // GitHub user/org. Empty for bundled.
  "url": "https://github.com/funatsufumiya/tcxLua.git",
  "bundled": false,                    // true if shipped inside TrussC core
  "version": "0.1.0",
  "latest_tag": "v0.1.0",
  "description": "...",
  "author": "...",
  "license": "MIT",
  "trussc_version": ">=0.5.0",
  "screenshot": "https://github.com/.../raw/HEAD/docs/preview.png",
  "demo_url": "https://...",
  "platforms": ["macos", "win", "linux", "web"],
  "keywords": ["..."],
  "dependencies": [...]
}
```

### Display Convention for Name Collisions

Two different repos can produce addons with the same `name` (for example, a bundled `tcxLua` and a community `funatsufumiya/tcxLua`). The registry intentionally keeps both. Consumers (trusscli, the website) should display them as:

- `bundled: true` → plain `tcxLua`
- `bundled: false` → `owner/name` (e.g. `funatsufumiya/tcxLua`)

`trusscli addon clone <name>` should accept both forms; a bare `<name>` that maps to multiple entries should prompt the user to disambiguate with `<owner>/<name>`.

### Discovery Safety

The crawler refuses to commit an empty or catastrophically reduced (>50%) `addons_list.json` to guard against transient GitHub Search indexing lag wiping the registry. When this safety floor triggers, the existing list is preserved until the next successful run.

### Addon Author Checklist

To make your addon discoverable:

1. Name it `tcxXxx` (camelCase after `tcx`).
2. Add `addon.json` at the repo root (even `{}` works to opt in).
3. Add the `trussc-addon` topic on the GitHub repo settings page.
4. Make sure the repo is public and not archived.

---

## Bundled Addon Details

### tcxBox2d

2D physics engine (Box2D).

**Features:**
- World (physics world)
- Body (physics body base class, inherits tc::Node)
- Circle, Rect, Polygon (shapes)
- Mouse drag (b2MouseJoint)

**Usage Example:**

```cpp
#include <tcxBox2d.h>

using namespace tc;
using namespace tcx::box2d;

class tcApp : public App {
    World world;
    Circle circle;

    void setup() override {
        world.setup(Vec2(0, 300));  // Gravity
        world.createBounds();        // Screen edge walls
        circle.setup(world, 400, 100, 30);
    }

    void update() override {
        world.update();
        circle.updateTree();  // Sync Box2D → Node coordinates
    }

    void draw() override {
        clear(0.12);
        setColor(1.0, 0.78, 0.39);
        circle.drawTree();    // Draw with position/rotation applied
    }
};
```

### tcxCurl

HTTPS client using libcurl (FetchContent).

**Features:**
- HTTP/HTTPS GET, POST, PUT, DELETE
- Header and body handling
- Async requests

### tcxGltf

glTF 2.0 / GLB model loader using cgltf.

**Features:**
- Load glTF 2.0 and GLB files
- Embedded textures, materials (PBR)
- Mesh, skeleton, animation support

### tcxHap

Hap video codec for fast GPU-accelerated playback.

**Features:**
- Hap, Hap Alpha, Hap Q codecs
- GPU-side decompression (S3TC/DXT)

### tcxImGui

Dear ImGui integration.

**Features:**
- Full Dear ImGui API
- Integrated with TrussC rendering pipeline

### tcxLut

3D LUT (Look-Up Table) color grading.

**Features:**
- Load .cube LUT files
- Apply color grading to Texture/Fbo

### tcxObj

Wavefront OBJ model import/export.

**Features:**
- Load .obj + .mtl files
- Phong-to-PBR material conversion
- Export meshes to OBJ format

### tcxOsc

OSC (Open Sound Control) protocol send/receive.

**Features:**
- OscSender (send OSC messages)
- OscReceiver (receive OSC messages)
- OscMessage, OscBundle (message construction)

### tcxQuadWarp

Quad warp / projection mapping.

**Features:**
- 4-corner perspective warping
- Interactive corner editing
- Save/load warp configuration

### tcxTls

TLS/SSL communication support (mbedTLS).

**Features:**
- Secure TCP communication
- Server certificate verification **required by default** (see [SECURITY.md](SECURITY.md))
- Custom CA bundle via `setCACertificate()` / `setCACertificateFile()`
- Dev-only opt-out via `setVerifyNone()` (don't ship)

### tcxWebSocket

WebSocket client and server.

**Features:**
- WebSocket client (ws:// and wss://)
- WebSocket server
- Text and binary messages
- For `wss://`: TLS cert verification **on by default**. Use
  `setTlsVerifyNone()` or `setTlsCACertificate(pem)` on the client if needed
  (see [SECURITY.md](SECURITY.md))
