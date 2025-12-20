# TrussC Build Guide

## Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler
- macOS: Xcode Command Line Tools (`xcode-select --install`)
- Windows: Visual Studio 2022 or MinGW
- Linux: GCC 10+ or Clang 10+

### VSCode / Cursor Extensions

If using VSCode or Cursor, install the following extensions:

| Extension | Purpose |
|-----------|---------|
| **CMake Tools** | CMake build integration |
| **CodeLLDB** | Debug execution (macOS/Linux) |
| **C/C++** | IntelliSense, code completion |

---

## Using Project Generator (Recommended)

TrussC includes a GUI-based project generation tool.

### 1. Build the Project Generator

Build the Project Generator itself (first time only).

**macOS:**
```bash
# Double-click tools/buildProjectGenerator_mac.command
# or
cd /path/to/tc_v0.0.1/tools/projectGenerator
mkdir build && cd build
cmake ..
cmake --build .
```

**Windows:**
```bash
# Double-click tools/buildProjectGenerator_win.bat
# or
cd /path/to/tc_v0.0.1/tools/projectGenerator
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### 2. Create a New Project

Launch the Project Generator.

![Project Generator - Create New](images/projectGenerator_generate.png)

1. **Project Name**: Enter project name
2. **Location**: Select save folder
3. **Addons**: Check addons to use
4. **IDE**: Select IDE to use
   - CMake only: Generate CMakeLists.txt only
   - VSCode: Also generate .vscode/launch.json and settings.json
   - Cursor: Same as VSCode
   - Xcode (macOS): Generate .xcodeproj with cmake -G Xcode
   - Visual Studio (Windows): Generate .sln with cmake -G "Visual Studio 17 2022"
5. Click **Generate Project**

### 3. Update Existing Projects

Click **Import** to load an existing project, which switches to Update mode.

![Project Generator - Update](images/projectGenerator_update.png)

- Add/remove addons
- Change IDE settings
- Switch TrussC version (change TrussC folder in Settings)

**Update Project** updates CMakeLists.txt and addons.make.
**Open in IDE** opens the project in the selected IDE.

### 4. Build and Run

**VSCode / Cursor:**
1. Open in IDE to open project
2. `F7` or `Cmd+Shift+P` → `CMake: Build`
3. `F5` for debug execution

**Xcode:**
1. Open in IDE to open .xcodeproj
2. `Cmd+R` to run

**Visual Studio:**
1. Open in IDE to open .sln
2. `F5` to run

---

## Adding Addons

Addons can be added in two ways.

### Method 1: Add via Project Generator (Recommended)

1. Import project in Project Generator
2. Check addons to use
3. Update Project

### Method 2: Edit addons.make

Edit `addons.make` in the project folder:

```
# TrussC addons - one addon per line
tcxBox2d
tcxSomeAddon
```

See [ADDONS.md](ADDONS.md) for details.

---

## Using CMake Directly (Advanced)

You can build directly from command line without the Project Generator.

### 1. Copy Template

```bash
cp -r /path/to/tc_v0.0.1/examples/templates/emptyExample ~/myProject
cd ~/myProject
```

### 2. Set TC_ROOT

Edit near the top of `CMakeLists.txt`:

```cmake
set(TC_ROOT "/path/to/tc_v0.0.1" CACHE PATH "Path to TrussC")
```

### 3. Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 4. Run

```bash
# macOS
open bin/myProject.app

# Windows
./bin/myProject.exe

# Linux
./bin/myProject
```

### Generate IDE Projects

```bash
# Xcode
cmake -G Xcode ..

# Visual Studio
cmake -G "Visual Studio 17 2022" ..
```

---

## Build Options

### Release Build

```bash
cmake --build . --config Release
```

### Clean Build

```bash
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```

---

## Icon Configuration

App icons can be set by simply placing an image in the `icon/` folder.

### Basic Usage (Recommended)

**Just add a PNG:**

```
myProject/
├── icon/
│   └── myicon.png    ← Just put a 512x512+ PNG here!
├── src/
└── CMakeLists.txt
```

Automatically converted to OS-specific format at build time.

### Supported Formats

| OS | Priority |
|----|----------|
| macOS | 1. `.icns` → 2. `.png` (auto-converted) → 3. Default |
| Windows | 1. `.ico` → 2. `.png` (auto-converted) → 3. Default |

- **macOS**: PNG auto-converted to `.icns` with sips + iconutil (no extra tools needed)
- **Windows**: PNG auto-converted to `.ico` with ImageMagick

### Recommended Specs

- **Size**: 512x512 pixels or larger (1024x1024 recommended)
- **Format**: PNG (transparency supported)
- **Filename**: Anything (uses first file found)

### PNG Auto-Conversion on Windows

To use PNG → ICO conversion on Windows, **ImageMagick** is required:

1. Download installer from [ImageMagick official site](https://imagemagick.org/script/download.php)
2. During installation, **check "Add application directory to your system path"**
3. Verify in command prompt:
   ```cmd
   magick --version
   ```

If ImageMagick is unavailable, the default icon is used automatically.
Pre-preparing an `.ico` file eliminates the need for conversion.

### Manual Preparation

You can also prepare platform-specific icons yourself instead of relying on PNG conversion:

```
icon/
├── app.icns    ← macOS
├── app.ico     ← Windows
└── app.png     ← Backup / Linux (future support)
```

### No Icon

If the `icon/` folder doesn't exist or is empty, TrussC's default icon is used.

---

## Distribution

Apps created with TrussC are statically linked, requiring no external DLLs.

### Distribution Structure

```
MyApp/
├── bin/
│   ├── MyApp.exe      (Windows)
│   ├── MyApp          (Linux)
│   └── MyApp.app/     (macOS)
└── data/              (if assets exist)
```

Zip the folder for distribution.

---

## Troubleshooting

### TC_ROOT Not Set

```
╔══════════════════════════════════════════════════════════════════╗
║   ERROR: TC_ROOT is not set!                                    ║
║   Use projectGenerator to create or update this project.        ║
╚══════════════════════════════════════════════════════════════════╝
```

→ Create/update project with Project Generator, or manually set TC_ROOT in CMakeLists.txt.

### TrussC Not Found

```
╔══════════════════════════════════════════════════════════════════╗
║   ERROR: TrussC not found!                                      ║
║   Looked in: /path/to/tc_v0.0.1                                 ║
╚══════════════════════════════════════════════════════════════════╝
```

→ Verify TC_ROOT path is correct. Reset TrussC folder in Project Generator Settings.

### CMake Not Found

```bash
# macOS (Homebrew)
brew install cmake

# Windows (winget)
winget install Kitware.CMake

# Linux
sudo apt install cmake
```

### Compiler Not Found (macOS)

```bash
xcode-select --install
```

### Build Errors

```bash
# Delete build folder and retry
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```
