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

# Drop a target again (also --no-android / --no-ios)
trusscli update -p path/to/myProject --no-web

# Switch the IDE (vscode, cursor, xcode, vs, cmake)
trusscli update -p path/to/myProject --ide cursor

# Specify TrussC root explicitly (if auto-detection fails)
trusscli update -p path/to/myProject --tc-root path/to/TrussC

# Generate a new project
trusscli new path/to/myNewApp
```

`trusscli build` configures the target's build folder itself when it has no
CMake cache (after `trusscli clean`, or a deleted folder) or only the cache
of a configure that failed, and prints one line saying so. On Windows it also
notices when Visual Studio changed since the project was generated (a pinned
MSVC, Windows SDK or ninja path in `CMakePresets.json` is gone): it detects
Visual Studio again, replaces only those paths in the `windows` preset (the
rest of the file stays), removes `build-windows` and configures again. When
no usable Visual Studio is found it changes nothing and says so.
`trusscli doctor` reports the same check.

`update`, `addon add` and `addon remove` keep the project's IDE, its Web /
Android / iOS targets and its web backend: they read them back from the
project's `CMakePresets.json` (the IDE is stored there as
`"vendor": {"trussc": {"ide": "..."}}`), then apply the flags you pass. So
`trusscli update --web` once is enough, and `--no-web` / `--no-android` /
`--no-ios` drop a target again. A kept target is configured again on every
regeneration. Its toolchain path saved in `CMakePresets.json` is reused when
the current shell has no emsdk / Android NDK set up and the file still
exists; if its configure fails anyway, that is a warning naming
`trusscli update --no-web` (or `--no-android` / `--no-ios`), and the rest of
the regeneration (e.g. the addon change) stands. A target you pass as a flag
must configure, or the command fails.
Scripts that want an exact target set pass every flag, as
`examples/build_all.py` does. `CMakePresets.json` is gitignored, so after a
fresh clone the defaults apply (`vscode`, native only) until you pass the
flags again. A saved setting that cannot be used — a file that does not
parse, an unknown IDE id, or an IDE this OS cannot generate (`xcode` off
macOS, `vs` off Windows, e.g. in a folder shared between machines) — is
reported with a warning and replaced by the default. `TC_WEB_BACKEND` is read
the way CMake builds it: `"WGPU"` (or unset) is WebGPU, any other value is
GLES3 (WebGL), with a warning unless it is `"GLES3"`.

### Keeping `trusscli` in sync

`trusscli` is a compiled binary. When you update TrussC by pulling the latest
source (`git pull`), the binary you already have is **not** rebuilt automatically,
so it can fall out of step with the framework.

```bash
# Pull + rebuild trusscli in one step (recommended)
trusscli upgrade
```

If you pull manually instead, rebuild `trusscli` afterwards
(`tools/build_mac.command` / `build_win.bat` / `build_linux.sh`). `trusscli`
detects this drift and prints a reminder on startup when the source has changed
since the binary was built:

```
Note: TrussC has changed since trusscli was built (trusscli v0.6.1 vs TrussC v0.7.0).
      Run 'trusscli upgrade' to rebuild trusscli in sync.
```

`trusscli doctor` reports the same version-sync status in detail.

---

## 2. Building Projects

TrussC uses **CMake Presets** to ensure consistent build configurations across platforms (macOS, Windows, Linux, Web).

> **CMake on Windows.** Installing CMake is recommended (`winget install
> Kitware.CMake`) — without it the build may fail in some cases. It is not
> strictly required, though: the CMake MSI ships with Visual Studio *inside* the
> VS install (not on `PATH`), and `trusscli` (`build`/`run`/`upgrade`/`doctor`)
> auto-detects the CMake bundled with the newest installed Visual Studio (or
> Build Tools) that has the "C++ CMake tools" component, prepending it to `PATH`
> for the build. So a `trusscli build` can succeed even when `cmake --version`
> reports "not found" in a plain shell. Raw `cmake --preset` invocations (below)
> still need cmake on `PATH` yourself. Run `trusscli doctor` to see which CMake
> is detected.

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

**Compiler warnings (opt-in):**

`trusscli build --warnings` (also `trusscli run --warnings`) enables `-Wall -Wextra`
(MSVC: `/W4`) on **your own** source files. TrussC, sokol/stb, and addon headers are
treated as system includes, so only your code is flagged — not framework internals.
There is **no `-Werror`**: warnings never break the build.

```bash
trusscli build --warnings
trusscli run --warnings
```

The flag is sticky in the CMake cache: once you build with `--warnings`, later builds
keep it on until you reconfigure without it (e.g. `trusscli update`).

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

**Custom AndroidManifest.xml:**

By default the build uses TrussC's bundled manifest, which declares every
permission the core APIs (`acquireWifiLock`, `acquireMulticastLock`,
`vibrate`, etc.) might need. This is convenient for local development but
not ideal for store-published apps or MDM-managed devices, where requesting
unused permissions can cause review pushback or policy rejection.

To override, drop your own manifest into the project at one of:

- `android/AndroidManifest.xml.in` — run through `configure_file` so
  `@TC_APP_PACKAGE@` and `@TC_APP_LIB_NAME@` still expand. Recommended.
- `android/AndroidManifest.xml` — copied verbatim (you fill in the
  `package` and `lib_name` yourself).

If neither file exists, the bundled "everything" manifest at
`core/resources/android/AndroidManifest.xml.in` is used unchanged.

Suggested workflow: copy the bundled manifest into `android/` and strip
out the `<uses-permission>` lines you don't actually need.

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

### Windows Application Manifest (UTF-8)
Every Windows executable built through `trussc_app()` embeds an application manifest (`core/resources/windows/app.manifest`), merged with the linker's default one:

- `activeCodePage` = `UTF-8`: on Windows 10 version 1903 or later the process code page is UTF-8, so narrow strings are UTF-8 in every API that takes them (`fs::path(std::string)`, `path.string()`, `fopen`, `getenv`, the `-A` Win32 functions), in TrussC, addons and third-party libraries alike. A UTF-8 literal or `std::string` works as a file path: `img.load("写真.png")`. Older Windows ignores the setting. There, and in an executable not built through `trussc_app()`, narrow strings are in the system code page (CP932, CP1252): convert a UTF-8 string with `utf8ToPath()` before passing it to `fs::path`, `load()` or `save()`. That includes the UTF-8 strings the path helpers return (`getFileName()`, `joinPath()`, `listDirectory()`, ...).
- `longPathAware`: paths longer than 260 characters work where Windows has long paths enabled (the `LongPathsEnabled` policy).

The console output code page is UTF-8 as well: `runApp()` (through `sapp_desc.win32.console_utf8`) and `runHeadlessApp()` switch it to UTF-8 while the app runs, so UTF-8 log text reads correctly in the console of a Debug or `TRUSSC_SHOW_CONSOLE` build. This is done at run time, not by the manifest. The previous code page comes back when `runApp()` / `runHeadlessApp()` returns, and when Ctrl+C or Ctrl+Break ends the app. In a headless app, the first Ctrl+C or Ctrl+Break stops the loop; a second one ends a hung app right away. `std::exit()`, `abort()`, an uncaught exception or a crash leave the console in UTF-8; `chcp` with the old number (e.g. `chcp 932`) sets it back.

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
| **RelWithDebInfo** | `-O2` | Yes | Disabled (`-DNDEBUG`, CMake's default) | **Default.** Development, debugging, installations |
| **Release** | `-O2`/`-O3` | No | Disabled | Minimal binary size for distribution |

`NDEBUG` also turns off sokol's validation layer (`SOKOL_DEBUG`), so only a **Debug** build reports sokol API misuse.

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

Currently, all state is reset on reload — `setup()` runs from scratch each time. Member variables, scene graph, loaded resources are all recreated, and hover, the mouse grab and the node selection (`getSelectedNode()`) start empty. This is the same model as Processing / p5.js live coding.

For most creative coding use cases (adjusting colors, positions, animations), this is sufficient.

State that outlives the App is the exception: singletons and function-local statics in your code (or in an addon) belong to the guest library, which stays loaded after a reload, so the previous build's copy keeps any listener it has on `events()`. Drop them on `events().hotReloadUnload`, which fires before the host unloads the current build while its App is still alive (and once more at exit, after `exit`):

```cpp
unloadListener_ = events().hotReloadUnload.listen([this] {
    // release what this build registered (the same cleanup as on exit)
});
```

tcxImGui and tcxNodeInspector do this themselves.

### Disabling Hot Reload

Comment out or delete the `TC_HOT_RELOAD` line:
```cpp
// TC_HOT_RELOAD(tcApp)   ← commented out
```
On the next build, cmake reconfigures back to a single static binary. The `TC_RUN_APP` macro in `main.cpp` automatically falls through to normal `runApp<>()`.

### Limitations

- **Supported platforms**: macOS (`.dylib`), Linux (`.so`), Windows (`.dll`). Wasm / iOS / Android fall back to static mode automatically.
- **Windows guest state**: the guest DLL compiles its own copy of every header-inline variable, so framework state that host and app code share lives in the host behind non-inline functions ([ARCHITECTURE.md §5.G](ARCHITECTURE.md#g-one-instance-per-process-header-inline-state)): MCP tools, events, timers, audio, recording, the main-thread queue, `setFps()` / `redraw()`, the clip / fov defaults, touch-as-mouse, the data path root, bitmap-font glyphs, the overlay (tcxImGui) queries, the debug counters behind `getNodeCount()` / `getTextureCount()` / `getFboCount()`, the current window context and the secondary windows' double-attach guard (so guest code sees the host's release when a window closes; a closed App is not attached again, so after that you attach a new App) all reach the host from a Windows guest too, and guest code sees the host's `WindowSettings::pixelPerfect` and sokol_gl budget. The GPU caches (FBO contexts and pipelines, IBL bake pipelines, font atlases and samplers) are the host's as well, so a reload reuses them instead of filling the host's sokol pools with a new set each time. What each module still keeps for itself is listed in `tools/header_state_allowlist.txt` with the reason it is harmless: warn-once flags and small derived caches (demangled type names). Addons' own header-inline state is the guest's by design.
- **Comment style**: Use `//` to disable. `/* */` block comments are not detected by the cmake scanner.
- **Build tool**: `trusscli build` handles hot reload state changes in one step. Raw `cmake --build` may require building twice when toggling `TC_HOT_RELOAD` on/off.
- **Build errors**: If the code doesn't compile, the previous version keeps running. Fix the error and save again.
