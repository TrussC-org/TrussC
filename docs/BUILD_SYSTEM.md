# TrussC Build System

TrussC uses a modern CMake-based build system designed to be automated via the **Project Generator**.

> ⚠️ **IMPORTANT**
>
> **Do NOT edit `CMakeLists.txt` or `CMakePresets.json` manually.**
> These files are regenerated every time by the Project Generator and your changes will be lost.
>
> - To add addons: Edit `addons.make` or use the Project Generator GUI.
> - To change project settings: Use the Project Generator.
> - For project-specific CMake config: Create `local.cmake` in your project root (see [Section 6](#6-project-local-cmake-config-localcmake)).

---

## 1. Project Generator

The **Project Generator** is the core tool for managing TrussC projects. It handles:
- Creating new projects
- Updating existing projects (e.g. after TrussC updates)
- Managing addons (`addons.make`)
- Generating IDE project files (VSCode, Xcode, Visual Studio)
- Configuring Web (WASM) builds

### GUI Mode
Run the `projectGenerator` app (built from `tools/`).
- **Create:** Select a name and path, choose addons, and click "Generate".
- **Update:** Use "Import" to select an existing project folder, modify settings, and click "Update".

### CLI Mode (Automation)
`trusscli` can be run from the command line for automation or headless environments.

```bash
# Update an existing project
trusscli update -p path/to/myProject

# Enable Web build (WASM)
trusscli update -p path/to/myProject --web

# Enable Android build
trusscli update -p path/to/myProject --android

# Enable iOS build
trusscli update -p path/to/myProject --ios

# Specify TrussC root explicitly (if auto-detection fails)
trusscli update -p path/to/myProject --tc-root path/to/TrussC

# Generate a new project
trusscli new path/to/myNewApp
```

---

## 2. Building Projects

TrussC uses **CMake Presets** to ensure consistent build configurations across platforms (macOS, Windows, Linux, Web).

### Using VSCode (Recommended)
1. Install **CMake Tools** extension.
2. Open the project folder.
3. Select a preset (e.g., `macos`, `windows`, `linux`, `web`) from the status bar or command palette.
4. Press `F7` (Build) or `F5` (Debug).

### Using Command Line

**Native Build (macOS/Linux/Windows):**
```bash
cmake --preset <os>   # e.g., macos, linux, windows
cmake --build --preset <os> --parallel
```

**Web Build (WASM):**
The Project Generator creates a helper script (`build-web.command`, `build-web.bat`, or `build-web.sh`) in your project folder. This script automatically handles Emscripten environment setup.

```bash
./build-web.command
```

Or manually using CMake:
```bash
# Requires Emscripten SDK to be set up
cmake --preset web
cmake --build --preset web --parallel
```

**Android Build (beta):**

Requires:
- Android SDK (`ANDROID_HOME` environment variable)
- Android NDK (`ANDROID_NDK_HOME` environment variable)
- Java (`JAVA_HOME` environment variable) — for APK signing

```bash
# Using trusscli (adds android preset to CMakePresets.json)
trusscli update -p path/to/myProject --android

# Build
cmake --preset android
cmake --build --preset android
# → APK is generated at bin/android/<project>.apk
# Upload to device via adb or other tools.
```

Notes:
- trusscli detects the NDK from `ANDROID_NDK_HOME` or `$ANDROID_HOME/ndk/`.
- APK signing uses `~/.android/debug.keystore`. If missing, APK packaging is skipped and only the .so is built.
- Touch input: On Android, touch events are delivered via `touchPressed()`/`touchMoved()`/`touchReleased()`. To also receive them as mouse events, call `setTouchAsMouse(true)` in `setup()`.
- Data files: Use `adb push` to transfer assets to the app's internal storage.
- **If `cmake --preset android` fails after trusscli update**, try running the command manually from the terminal.

**iOS Build (beta):**

Requires:
- Xcode (full installation, not just Command Line Tools)
- Apple Developer account (for device deployment)

```bash
# Using trusscli (adds ios preset)
trusscli update -p path/to/myProject --ios

# Generate Xcode project
cmake --preset ios

# Open in Xcode
open xcode-ios/*.xcodeproj
```

Then in Xcode:
- Select your device or simulator as the build target
- **Set your Development Team** in Signing & Capabilities (required — build will fail without it)
- Press ⌘R to build and run

Notes:
- Touch input works the same as Android (`touchPressed`/`touchMoved`/`touchReleased`).
- `setTouchAsMouse(true)` is ON by default on iOS, same as Android.
- System sensors (accelerometer, gyroscope, compass, etc.) are available via `tc::getAccelerometer()` etc.
- Screen brightness: iOS returns linear 0.0-1.0 matching the slider. Android returns a gamma-corrected value (see API docs).
- **First launch may show a black screen for up to 30 seconds** before the app appears. This is a known issue with initial Metal/GPU setup. Subsequent launches are faster.
- Frame rate may be very low for the first few seconds after launch. This stabilizes quickly.

### Building All Examples
To build all examples in the repository (useful for testing):

```bash
cd examples
./build_all.sh          # Native build only
./build_all.sh --web    # Native + Web build
./build_all.sh --clean  # Clean rebuild
```
This script automatically uses `trusscli` to update each example before building.

---

## 3. Project Structure

A standard TrussC project looks like this:

```
myProject/
├── addons.make          # List of used addons (User editable)
├── bin/                 # Output executables & assets
│   ├── data/            # Place your assets here (images, fonts, etc.)
│   ├── myProject.app    # (macOS)
│   ├── myProject.exe    # (Windows)
│   ├── myProject.html   # (Web)
│   └── android/         # (Android APK)
├── build-macos/         # Build artifacts (do not touch)
├── build-android/       # Build artifacts (do not touch)
├── build-web/           # Build artifacts (do not touch)
├── CMakeLists.txt       # AUTO-GENERATED (Do not edit)
├── CMakePresets.json    # AUTO-GENERATED (Do not edit)
├── local.cmake          # Project-local CMake config (optional, user editable)
├── icon/                # App icon (.icns, .icon, .ico, .png)
└── src/                 # Source code
    ├── main.cpp
    └── tcApp.cpp
```

### Entry Point (`main.cpp`)

All TrussC projects use `TC_RUN_APP` to start the app:
```cpp
int main() {
    tc::WindowSettings settings;
    return TC_RUN_APP(tcApp, settings);
}
```
`TC_RUN_APP` is a drop-in replacement for `tc::runApp<>()` that adds support for [hot reload](#7-hot-reload-development). In normal builds it behaves identically to `runApp<>()` with zero overhead.

> **Migration note:** If your project uses the older `tc::runApp<tcApp>(settings)` syntax, replace it with `TC_RUN_APP(tcApp, settings)`. Both work, but `TC_RUN_APP` enables hot reload when you opt in later.

### Data Folder
Place assets (images, fonts, sounds) in `bin/data/`.
This path is automatically resolved at runtime via `tc::getDataPath()`.

### App Icon
Place icon files in the `icon/` folder:

- **macOS:**
  - `.icns` - Traditional icon format (required for older macOS)
  - `.icon` - New format for macOS 26 Tahoe+ (created with Icon Composer, requires Xcode)
  - Both can coexist for compatibility across macOS versions
- **Windows:**
  - `.ico` - Windows icon format
  - `.png` - Converted to `.ico` automatically (requires ImageMagick)

---

## 4. Addon System

### Using Addons
Addons are libraries located in `TrussC/addons/`. To use an addon, add its name to `addons.make` in your project folder:

```
# Physics
tcxBox2d

# Networking
tcxOsc
```

Then run **Project Generator** (Update) to apply changes.

### Creating Addons
An addon is simply a folder in `TrussC/addons/`.

**Simple Addon:**
```
tcxMyAddon/
├── src/           # Source files (auto-collected)
│   ├── tcxMyAddon.h
│   └── tcxMyAddon.cpp
└── libs/          # External libraries (optional)
```

**Complex Addon (with CMakeLists.txt):**
If you need custom build logic, add a `CMakeLists.txt` in the addon root. It will be included via `add_subdirectory()`.

---

## 5. Under the Hood

The `trussc_app()` CMake macro (in `core/cmake/trussc_app.cmake`) handles:
*   Recursively collecting source files from `src/`
*   Setting C++20 standard
*   Linking `tc::TrussC` core library
*   Applying addons defined in `addons.make`
*   Loading `local.cmake` if it exists (see below)
*   Configuring platform-specific bundles (macOS .app, Windows resource files)

The **Project Generator** ensures that `CMakePresets.json` is correctly configured with the absolute path to your TrussC installation (`TRUSSC_DIR`), so you can move your project folder anywhere without breaking the build.

### macOS Deployment Target

TrussC requires **macOS 14.0 (Sonoma)** or later. This is because sokol's display backend uses `CADisplayLink` and `CAFrameRateRange` (introduced in macOS 14.0) for proper frame rate control, including ProMotion 120Hz support.

The Project Generator automatically sets `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` in both the `macos` and `xcode` presets.

### Build Type

TrussC defaults to **RelWithDebInfo** (Release with Debug Info). This provides optimized performance (`-O2`) while keeping debug symbols for stack traces and breakpoint debugging. We believe this is the best default for creative coding — you get near-Release speed without losing the ability to debug.

| Build Type | Optimization | Debug Symbols | assert() | Use Case |
|---|---|---|---|---|
| **Debug** | None (`-O0`) | Yes | Enabled | Step-through debugging of optimized-out variables |
| **RelWithDebInfo** | `-O2` | Yes | Enabled | **Default.** Development, debugging, installations |
| **Release** | `-O2`/`-O3` | No | Disabled | Minimal binary size for distribution |

For most users, the default RelWithDebInfo is sufficient. If you need a full Debug build (e.g., when variables are optimized out during step-through debugging):

```bash
cmake -DCMAKE_BUILD_TYPE=Debug --preset macos
cmake --build --preset macos --parallel
```

> **Note:** Xcode and Visual Studio are multi-config generators and support switching between Debug/Release directly in the IDE without reconfiguring.

---

## 6. Project-Local CMake Config (`local.cmake`)

For project-specific build settings that don't belong in a shared addon, you can create a `local.cmake` file in your project root. It is automatically included by `trussc_app()` after addons are applied.

This is useful for:
- Linking system libraries installed via package managers (e.g. `brew`, `apt`)
- Adding project-specific compile definitions
- Any CMake configuration that only applies to this project

### Example

```cmake
# local.cmake - link system-installed libraries

find_package(PkgConfig REQUIRED)

# exiv2 (EXIF metadata)
pkg_check_modules(EXIV2 REQUIRED exiv2)
target_include_directories(${PROJECT_NAME} PRIVATE ${EXIV2_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${EXIV2_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${EXIV2_LIBRARIES})

# lensfun (lens correction)
pkg_check_modules(LENSFUN REQUIRED lensfun)
target_include_directories(${PROJECT_NAME} PRIVATE ${LENSFUN_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${LENSFUN_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LENSFUN_LIBRARIES})
```

### `local.cmake` vs Addons

| | `local.cmake` | Addon (`addons.make`) |
|--|---------------|----------------------|
| Scope | This project only | Shared across projects |
| Location | Project root | `TrussC/addons/` |
| Reusability | Not reusable | Reusable by any project |
| Use case | System library linking, project-specific flags | Reusable wrappers, FetchContent libraries |

**Rule of thumb:** If only one project uses it, put it in `local.cmake`. If multiple projects could benefit, make it an addon.

---

## 7. Hot Reload (Development)

Edit C++ code and see changes reflected in a running app within seconds — no restart needed.

### Quick Start

1. Add `TC_HOT_RELOAD(tcApp)` to the top of your app's `.cpp` file:
   ```cpp
   // tcApp.cpp
   #include "tcApp.h"
   TC_HOT_RELOAD(tcApp)

   void tcApp::setup() { ... }
   void tcApp::draw() { ... }
   ```

2. Build and run as usual. On the first build after adding `TC_HOT_RELOAD`, cmake will automatically reconfigure to enable hot reload.

3. While the app is running, edit and save any source file in `src/`. The change is compiled and loaded within 1-3 seconds.

### How It Works

When `TC_HOT_RELOAD` is detected in a source file, the build splits into two targets:

- **Host (EXE)**: `main.cpp` + TrussC core. Owns the window, event loop, and file watcher.
- **Guest (shared library)**: Your app code (`tcApp.cpp` etc.). Rebuilt on every file change.

The Host monitors `src/` for file modifications (polling every 500ms). When a change is detected:
1. Guest is rebuilt via `cmake --build --target guest` (incremental — only your code, not TrussC core)
2. Old Guest is unloaded (`dlclose` / `FreeLibrary`)
3. New Guest is loaded (`dlopen` / `LoadLibrary`)
4. A new App instance is created → `setup()` runs again

### State Reset (Stage 1)

Currently, all state is reset on reload — `setup()` runs from scratch each time. Member variables, scene graph, loaded resources are all recreated. This is the same model as Processing / p5.js live coding.

For most creative coding use cases (adjusting colors, positions, animations), this is sufficient.

### Disabling Hot Reload

Comment out or delete the `TC_HOT_RELOAD` line:
```cpp
// TC_HOT_RELOAD(tcApp)   ← commented out
```
On the next build, cmake reconfigures back to a single static binary. The `TC_RUN_APP` macro in `main.cpp` automatically falls through to normal `runApp<>()`.

### Limitations

- **Supported platforms**: macOS (`.dylib`), Linux (`.so`), Windows (`.dll`). Wasm / iOS / Android fall back to static mode automatically.
- **Comment style**: Use `//` to disable. `/* */` block comments are not detected by the cmake scanner.
- **Build tool**: `trusscli build` handles hot reload state changes in one step. Raw `cmake --build` may require building twice when toggling `TC_HOT_RELOAD` on/off.
- **Build errors**: If the code doesn't compile, the previous version keeps running. Fix the error and save again.
