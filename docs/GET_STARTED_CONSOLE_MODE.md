# TrussC on Headless Linux (Console Mode)

Guide for running TrussC on Linux systems without a desktop environment — Raspberry Pi Lite, Orange Pi, Arch ARM, and other single-board computers (SBCs) or headless servers.

## Overview

TrussC apps fall into two categories on headless systems:

| App Type | Display Server Needed? | Example |
|---|---|---|
| **Headless** (no window) | No | `noWindowExample`, MCP servers, network tools |
| **GUI** (window + graphics) | Yes | Graphics examples, interactive installations |

Headless apps work out of the box. GUI apps need a display session — see [Section 3](#3-running-gui-apps-with---session) below.

---

## 1. Build trusscli

Build the TrussC CLI tool. This also installs build dependencies automatically (`install_dependencies_linux.sh` is called internally):

```bash
cd TrussC/tools
chmod +x build_linux.sh
./build_linux.sh
```

At the end, the script asks whether to create a `/usr/local/bin/trusscli` symlink — answer `y` so `trusscli` is available from anywhere.

This installs compilers, CMake, and the development libraries needed to build TrussC. It does **not** install any display server packages — those are only needed if you want to run GUI apps (see [Section 3](#3-running-gui-apps-with---session)).

---

## 2. Building and Running Headless Apps

Headless apps run without any display server — no window, no GPU context. The `draw()` callback is **never called**; only `setup()` and `update()` run.

```bash
# Build a headless example
trusscli build -p examples/windowing/noWindowModeExample

# Run directly — no display needed
./examples/windowing/noWindowModeExample/bin/noWindowModeExample
```

### How headless mode works

In headless mode (`runHeadlessApp<>()`), `update()` is called at a fixed rate (default **60 Hz**). There is no window, no GPU context, and `draw()` is not invoked.

```cpp
// main.cpp — headless entry point
#include "tcApp.h"

int main() {
    return tc::runHeadlessApp<tcApp>(
        tc::HeadlessSettings().setFps(60)   // default: 60 Hz
    );
}
```

You can adjust the update rate:

```cpp
// Lower rate for a network server that doesn't need 60 Hz
tc::HeadlessSettings().setFps(10)    // 10 Hz — saves CPU

// Event-driven: update only when explicitly triggered
tc::HeadlessSettings().setFps(tc::EVENT_DRIVEN)
```

With `EVENT_DRIVEN` (0 Hz), `update()` is not called automatically — use timers, network callbacks, or `redraw()` to trigger processing manually.

### Use cases

- **MCP server mode** (`TRUSSC_MCP=1`) — AI-driven automation without a screen
- **Network tools** — OSC receivers, TCP/UDP servers
- **Batch processing** — image/video processing, audio generation
- **Sensor data collection** — read sensors on an SBC, log to file or send over network

---

## 3. Running GUI Apps with `--session`

To run GUI apps on a headless system, `trusscli run --session <backend>` launches a minimal display session just for your app — no full desktop environment needed.

### Option A: X11

The simplest option. Works well and avoids Wayland-specific issues (e.g., surface throttling when backgrounded).

**Install X11:**
```bash
# Debian / Raspbian / Ubuntu
sudo apt install xserver-xorg xinit

# Arch
sudo pacman -S xorg-server xorg-xinit
```

**Run:**
```bash
trusscli run --session x11
```

### Option B: labwc (Wayland)

A lightweight Wayland compositor. Useful for installations that need Wayland-specific features.

**Install labwc:**
```bash
# Debian / Raspbian
sudo apt install labwc

# Arch
sudo pacman -S labwc
```

**Permissions** — your user needs access to input and video devices:
```bash
sudo usermod -aG video,input,render $USER
# Log out and back in for changes to take effect
```

You may also need polkit:
```bash
sudo apt install policykit-1    # Debian / Raspbian
sudo pacman -S polkit            # Arch
```

**Run:**
```bash
trusscli run --session labwc
```

### Which to choose?

| | X11 | labwc (Wayland) |
|---|---|---|
| **Setup** | Simple | Requires group permissions + polkit |
| **Compatibility** | Mature, widely tested | Newer, some edge cases |
| **AV sync** | Stable | May throttle backgrounded surfaces ([#29](https://github.com/TrussC-org/TrussC/issues/29)) |
| **Recommended for** | Most use cases | Wayland-specific projects |

**Recommendation: start with X11.** Switch to labwc if you need Wayland features.

---

## 4. Auto-start at Boot

For installations and kiosk setups, you can auto-start your app on boot using a systemd service:

```bash
sudo tee /etc/systemd/system/trussc-app.service << 'EOF'
[Unit]
Description=TrussC App
After=network.target

[Service]
Type=simple
User=your_username
WorkingDirectory=/path/to/your/project
ExecStart=/usr/local/bin/trusscli run --session x11
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable trussc-app
sudo systemctl start trussc-app
```

Replace `/path/to/your/project` with your project directory and `your_username` with your user.

---

## 5. SBC-Specific Tips

### Raspberry Pi (Raspbian Lite)

- **Recommended OS**: Raspberry Pi OS Lite (64-bit)
- **GPU**: VideoCore VI (Pi 4) or VideoCore VII (Pi 5) — TrussC uses OpenGL ES 3.0
- **HW video decode** (auto-detected by TrussC, falls back to SW):
  - **Pi 4**: V4L2M2M interface exposes H.264 / HEVC HW decoder
  - **Pi 5**: H.264 HW decoder removed; HEVC still available via DRM (prime interop)
- **Boot to CLI**: `sudo raspi-config` → System Options → Boot / Auto Login → Console
- **Remote access**: SSH for development, physical display for final output

### Orange Pi / Other Allwinner SBCs

- Install ARM-compatible Mali GPU drivers (Allwinner SoCs use Mali exclusively)
- `install_dependencies_linux.sh` should cover build deps on Debian-based distros
- Use `--session x11` for display; Wayland support varies by GPU driver

### Arch Linux ARM

- Use `pacman` for dependencies:
  ```bash
  sudo pacman -S cmake gcc mesa libx11 libxrandr libxinerama libxcursor libxi gtk3
  ```
- X11 is generally more stable than Wayland on ARM Arch
- AUR may have additional GPU-specific packages

---

## 6. Troubleshooting

### `trusscli run --session x11` fails with "Server is already active"

An X server is already running (desktop session or previous crashed session):
```bash
# Check for existing X processes
ps aux | grep -i xorg

# Remove stale lock if X is not actually running
sudo rm /tmp/.X0-lock
```

### `trusscli run --session labwc` fails with "Could not take control of session"

Seat permission issue:
```bash
# Add yourself to required groups
sudo usermod -aG video,input,render $USER

# Install polkit if not present
sudo apt install policykit-1

# Log out and back in
```

### Build fails with missing OpenGL headers

```bash
# Debian / Raspbian
sudo apt install libgles2-mesa-dev libegl1-mesa-dev

# Arch
sudo pacman -S mesa
```

### App runs but no display output (labwc, no physical monitor)

This is expected — labwc creates a Wayland session without a physical display. The app runs in the background. Connect a monitor to see the output, or use MCP mode (`TRUSSC_MCP=1`) for headless interaction.

---

## See Also

- [GET_STARTED.md](GET_STARTED.md) — General getting started guide
- [BUILD_SYSTEM.md](BUILD_SYSTEM.md) — Build system details and hot reload
- [AI_AUTOMATION.md](AI_AUTOMATION.md) — MCP mode for headless AI interaction
