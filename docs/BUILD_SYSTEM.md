# TrussC Build System

TrussC uses a modern CMake-based build system designed to be automated via the **Project Generator**.

> ⚠️ **IMPORTANT**
>
> **Do NOT edit `CMakeLists.txt` or `CMakePresets.json` manually.**
> These files are automatically generated and managed by the Project Generator.
>
> - To add addons: Edit `addons.make` or use the Project Generator GUI.
> - To change project settings: Use the Project Generator.

---

## 1. Project Generator

The **Project Generator** is the core tool for managing TrussC projects. It handles:
- Creating new projects
- Updating existing projects (e.g. after TrussC updates)
- Managing addons (`addons.make`)
- Generating IDE project files (VSCode, Xcode, Visual Studio)
- Configuring Web (WASM) builds

### GUI Mode
Run the `projectGenerator` app (found in `trussc/projectGenerator/`).
- **Create:** Select a name and path, choose addons, and click "Generate".
- **Update:** Use "Import" to select an existing project folder, modify settings, and click "Update".

### CLI Mode (Automation)
The Project Generator can be run from the command line for automation or headless environments.

```bash
# Update an existing project
projectGenerator --update path/to/myProject

# Enable Web build (WASM)
projectGenerator --update path/to/myProject --web

# Specify TrussC root explicitly (if auto-detection fails)
projectGenerator --update path/to/myProject --tc-root path/to/TrussC

# Generate a new project
projectGenerator --generate --name myNewApp --dir path/to/projects
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
cmake --build --preset <os>
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
cmake --build --preset web
```

### Building All Examples
To build all examples in the repository (useful for testing):

```bash
cd examples
./build_all.sh          # Native build only
./build_all.sh --web    # Native + Web build
./build_all.sh --clean  # Clean rebuild
```
This script automatically uses `projectGenerator` to update each example before building.

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
│   └── myProject.html   # (Web)
├── build-macos/         # Build artifacts (do not touch)
├── build-web/           # Build artifacts (do not touch)
├── CMakeLists.txt       # AUTO-GENERATED (Do not edit)
├── CMakePresets.json    # AUTO-GENERATED (Do not edit)
├── icon/                # App icon (512x512 png)
└── src/                 # Source code
    ├── main.cpp
    └── tcApp.cpp
```

### Data Folder
Place assets (images, fonts, sounds) in `bin/data/`.
This path is automatically resolved at runtime via `tc::getDataPath()`.

### App Icon
Place a 512x512+ PNG file in the `icon/` folder.
- **macOS:** Converted to `.icns` automatically (requires Xcode tools).
- **Windows:** Converted to `.ico` automatically (requires ImageMagick).

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

The `trussc_app()` CMake macro (in `trussc/cmake/trussc_app.cmake`) handles:
*   Recursively collecting source files from `src/`
*   Setting C++20 standard
*   Linking `tc::TrussC` core library
*   Applying addons defined in `addons.make`
*   Configuring platform-specific bundles (macOS .app, Windows resource files)

The **Project Generator** ensures that `CMakePresets.json` is correctly configured with the absolute path to your TrussC installation (`TRUSSC_DIR`), so you can move your project folder anywhere without breaking the build.
