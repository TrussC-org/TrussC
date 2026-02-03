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

### Official Addons

Addons included with TrussC:

| Addon Name | Description |
|------------|-------------|
| tcxBox2d | Box2D 2D physics engine |
| tcxOsc | OSC protocol send/receive |
| tcxTls | TLS/SSL communication (mbedTLS) |

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
| tcxOsc | `tcx::osc` |

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

## Official Addon List

### tcxBox2d

TrussC wrapper for Box2D 2D physics engine.

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
        clear(30);
        setColor(255, 200, 100);
        circle.drawTree();    // Draw with position/rotation applied
    }
};
```

### tcxOsc

OSC (Open Sound Control) protocol send/receive.

**Features:**
- OscSender (send OSC messages)
- OscReceiver (receive OSC messages)
- OscMessage, OscBundle (message construction)

### tcxTls

TLS/SSL communication support (using mbedTLS).

**Features:**
- Secure TCP communication
- Certificate verification
