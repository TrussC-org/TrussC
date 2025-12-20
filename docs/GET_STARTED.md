# Getting Started with TrussC

TrussC is a lightweight creative coding framework inspired by openFrameworks.
Built on C++20 + sokol, it's simple to write and runs cross-platform.

---

## 1. Set Up Your Environment

### Requirements

| OS | Compiler |
|----|----------|
| macOS | Xcode Command Line Tools (`xcode-select --install`) |
| Windows | Visual Studio 2022 |
| Linux | GCC 10+ or Clang 10+ |

**CMake** is also required:
```bash
# macOS
brew install cmake

# Windows
winget install Kitware.CMake

# Linux
sudo apt install cmake
```

### Editor Setup (VSCode / Cursor)

Install the following extensions:

| Extension | Purpose | Required OS |
|-----------|---------|-------------|
| **CMake Tools** | Build integration | All |
| **C/C++** | IntelliSense + debugging | All (Windows uses this for debugging) |
| **CodeLLDB** | Debug execution | macOS / Linux |

---

## 2. Build the Project Generator

Build the project creation tool (first time only).

**macOS:** Double-click `projectGenerator/buildProjectGenerator_mac.command`

**Windows:** Double-click `projectGenerator/buildProjectGenerator_win.bat`

---

## 3. Create a Project

![Project Generator](images/projectGenerator_generate.png)

1. Enter a **Project Name**
2. Select a **Location** to save
3. Choose `Cursor` or `VSCode` as **IDE**
4. Click **Generate Project**

---

## 4. Build and Run

1. Click **Open in IDE** to open the project
2. Press `F7` to build (or `Cmd+Shift+B` / `Ctrl+Shift+B`)
3. Press `F5` to run

That's it!

---

## 5. Run Examples

The `examples/` folder contains many samples.

```
examples/
├── graphics/      # 2D drawing
├── 3d/            # 3D drawing
├── sound/         # Sound
├── network/       # Networking
├── gui/           # ImGui
└── ...
```

Run examples the same way:
1. Click **Import** in Project Generator
2. Select an example folder
3. **Open in IDE** → `F5`

---

## 6. Write Code

Basic structure:

```cpp
// tcApp.h
#pragma once
#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
};
```

```cpp
// tcApp.cpp
#include "tcApp.h"

void tcApp::setup() {
    // Initialization
}

void tcApp::update() {
    // Called every frame
}

void tcApp::draw() {
    clear(30);  // Background color

    setColor(colors::white);
    drawCircle(getWidth()/2, getHeight()/2, 100);
}
```

---

## Next Steps

- [TrussC_vs_openFrameworks.md](TrussC_vs_openFrameworks.md) - API mapping for oF users
- [HOW_TO_BUILD.md](HOW_TO_BUILD.md) - Detailed build instructions, icons, distribution
- [ADDONS.md](ADDONS.md) - How to use addons
- [DESIGN.md](DESIGN.md) - Internal design details
