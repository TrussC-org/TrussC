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

Linux JPEG decoding requires the system **libjpeg-turbo** through `pkg-config libjpeg`.
The installer includes `libjpeg-dev` (Ubuntu: `libjpeg-turbo8-dev`;
Debian/Pi OS: `libjpeg62-turbo-dev`). To install it manually:

```bash
sudo apt install libjpeg-dev
```

Configure fails if the package is missing or lacks `JCS_EXT_RGBA`; there is no
stb fallback for JPEG on Linux. Other image formats and other platforms keep stb_image.

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

Project and binary names replace each character outside ASCII letters, digits, `_ . + -` with one `_` (a non-ASCII character such as 日 counts as one); reserved names get `app_`, and the folder keeps its name.

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

### Build with a project's pinned TrussC version

The installed trusscli builds the sources selected by a project's `.trussc-version`.
Share this file with the project so collaborators select the same sources.
Features newer than the pinned TrussC version may not apply; features that would
change the result stop with a message instead. Select a newer core to use them:

```bash
trusscli use v0.8.1
trusscli build --release
```

For example, an addon pinned with `@ref` needs a core that can load external
addon paths. An older core stops with `cannot load external addon paths
(TRUSSC_ADDON_PATHS is unavailable); select a newer TrussC version`.
Select a core with that support, such as `main`:

```bash
trusscli use main
```

`use` saves the selection and prepares its sources; `update` refreshes them and
regenerates project files. Tags, branches, full commit IDs, unique short commit
IDs (7–39 hexadecimal characters), and forks (`owner/repo@ref`) are supported.
Tags take priority over branches, and branches over short commit IDs. A short
commit ID is printed as `Resolved <short> to <full>` and saved as the full
40-character ID in `.trussc-version` and the cache folder name. Short commit IDs
in external addon `@ref` selections also save the full ID in `addons.make`.
For example:

```bash
trusscli use 006a086b
```

With Git, an ambiguous commit ID lists candidates and stops. Use a longer ID from that
list; IDs shorter than 7 characters are treated as ref names:

```bash
trusscli use <longer-commit-id>
```

Without Git, short commit IDs are resolved once through the GitHub commits API,
then the archive is fetched using the full ID. This works for external addons
without archive commit metadata. If the API rate limit is reached (60 requests
per hour without authentication), use a full ID or install Git:

```bash
trusscli use <40-character-commit-id>
```

### Run a Web build locally or on the LAN

`trusscli run --web` builds the Web project and starts a server on localhost.
Use `--host` to listen on all interfaces, or `--host <addr>` to listen on a
specific address. For a native run, `--host` prints a warning and is ignored.

```bash
trusscli run --web
trusscli run --web --host
trusscli run --web --host 127.0.0.1
```

The server uses emrun when available, otherwise Python's HTTP server. A `Network:`
URL is printed for each up, non-loopback, non-link-local address the server listens
on, so with `--host` there is one line per network adapter.

emrun listens on IPv4 only, so with emrun installed an IPv6 `--host` value such as
`::1` fails to start. Pass an IPv4 address instead:

```bash
trusscli run --web --host 192.168.1.20
```

### Build a cloned project and download missing sources

`trusscli build` checks the selected TrussC version and external addon refs before
configuring. When sources are missing from the cache, it lists their
`owner/repository@ref` identities and asks `Download now? [Y/n]`. Return or `y`/`Y`
runs `trusscli update`, including expansion of short commit pins, then builds.
`n`/`N` stops with the existing unavailable-version error. Cached projects build
without a question. `trusscli run` uses the same confirmation before building and launching.

After cloning a project, build interactively:

```bash
trusscli build
trusscli run
```

Without a terminal on stdin, downloads require `-y` / `--yes`. Use this in scripts
and CI; CI environment variables do not affect confirmation:

```bash
trusscli build --yes
trusscli run -y
```

`trusscli update` and `trusscli use` remain explicit downloads without a question.
To prepare sources separately:

```bash
trusscli update
trusscli build --release
```

### Supported TrussC layouts

TrussC versions with the old `trussc/` layout (v0.1.0–v0.4.4) stop with
“TrussC v0.4.4 predates the core/ layout; trusscli supports v0.5.0 and later.
Run `trusscli use v0.5.0` (or newer).” The layout check also applies to forks
and commits. A rejected `use` leaves the project's previous pin and local
selection unchanged and does not publish the unsupported cache entry.
Choose a supported version:

```bash
trusscli use v0.5.0
```

`use-local` saves a machine-local checkout override in project metadata and
selects sources for the installed trusscli. This override takes precedence over the
shared version file:

```bash
trusscli use-local /path/to/TrussC
trusscli build --release
```

Return to the shared pin with `use-local --clear`. Remove both selections with
`use --clear` to return to the current installation:

```bash
trusscli use-local --clear
trusscli use --clear
```

Cached versions are shared across projects. Inspect them with `cache list` and
remove an unused version with `cache remove <spec>`. An exact ref match wins;
otherwise, a unique short commit ID (7–39 hex characters) can match a cached
full 40-hex commit ID.
Branch and tag names must match exactly:

```bash
trusscli cache remove main
trusscli cache remove 006a086b
```

If several cached full commit IDs match the short ID, the command lists them and
removes nothing. Choose a longer commit ID or a full spec from `cache list`:

```bash
trusscli cache list
trusscli cache remove <owner/repo@full-ref>
```

Removing a selected cached
version requires preparing it again before the next build:

```bash
trusscli cache list
trusscli cache remove v0.8.1
trusscli update
```

`cache autoremove` offers to remove versions unused for at least 90 days;
`cache clear` offers to remove the whole cache. Both ask for confirmation:

```bash
trusscli cache autoremove
trusscli cache clear
```

### Keeping `trusscli` in sync

`trusscli` prints a reminder on every command when its binary inputs at HEAD
have changed since it was configured: `tools/src`, `tools/CMakeLists.txt`,
`tools/addons.make`, and `tools/icon`. Rebuild it with:

```bash
trusscli upgrade
```

The comparison uses the installed checkout selected by `TRUSSC_DIR` or the
executable location, independently of a project's pinned TrussC version.
Commits affecting only core, addons, tests, or other tools files, and uncommitted
edits anywhere, do not trigger the reminder. Without Git or known build metadata,
the reminder stays silent. There is no tcApp API for this CLI check.

`trusscli version` and `trusscli doctor` still show full Git describe strings;
doctor's Version sync result compares the same binary inputs.

For unattended trusscli builds with dependencies installed, run `./tools/build_linux.sh --no-deps --jobs 2 --non-interactive --no-launch` (macOS: `./tools/build_mac.command`, where `--no-deps` is a no-op).

On Windows, `upgrade` runs `tools/build_win.bat`, which sets up Visual Studio
and rebuilds both `tools/bin/trusscli.exe` (the PATH location) and the
`tools/trusscli.exe` link or copy. The running executable is renamed to
`trusscli.exe.old` before building; the next start removes it. If the build
fails, the previous executable is restored and manual rebuild instructions
are printed.

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

For a plain configure outside the TrussC repository, provide the core path:

```bash
cmake -S . -B build-manual -DTRUSSC_DIR="/path/to/TrussC/core"
cmake --build build-manual --parallel
```

The generated CMakeLists keeps `../../../core` as its fallback. If that path is
missing, configuration stops with an error naming `TRUSSC_DIR`, `trusscli update`,
and the host preset (`macos`, `linux`, or `windows`). Run `trusscli update` to
regenerate the project files and presets after moving your TrussC installation.

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

**Parallel jobs:**

`trusscli build -j N` (also `--jobs N`, and the same on `trusscli run` and `trusscli upgrade`)
runs at most N compile jobs at once, for example to keep several builds side by side within
RAM. trusscli passes it as `cmake --build … --parallel=N` on every OS. Without `-j` the build
tool decides (Ninja: CPU threads + 2), except on Linux, where trusscli limits the count by
available RAM (about 1.5 GB per job). An environment `CMAKE_BUILD_PARALLEL_LEVEL` has no
effect, because trusscli always passes `--parallel`.

`trusscli build --dry-run` prints the cmake commands it would run (configure and build), one
per line starting with `[dry-run]`, without running them.

```bash
trusscli build -j 4
trusscli build -j 4 --dry-run
```

**Build output:**

`trusscli build` and `trusscli run` print a short result, not every cmake line:

```
Building myApp (44 files) .........50%.........100%
Done: 2 files, 42 up to date (0m30s). Full log: build-linux/build.log
```

- One progress line, with a dot per 5 %.
- Only known noise is hidden: CMake `-- ...` configure lines, `[ NN%] Building` / `Linking` lines, every Ninja `[n/total] ...` status line, `Scanning dependencies`, `Built target`, CMake's `Preset ... variables:` block and, on Windows, the Visual Studio `vcvarsall` banner. After Ninja's `FAILED: <target>` line, the compiler command line is hidden too (the log has it). Everything else is shown as the tool printed it, so warnings, errors, compiler notes and linker messages are never lost.
- The last lines are the result: `Done:` or `Failed:`, then `ERROR: N (files)` and `WARNING: N (files)` when there are any (`ERROR: link failed` for a link error without a source file).
- The full output of the configure and build is always saved to `<build folder>/build.log` (for example `build-linux/build.log`), and overwritten by the next build.
- `trusscli build --verbose` (also `run`, `update`) prints the full output on screen as well.
- `trusscli run` shortens only the build. The app's own output is not filtered.

Warnings and errors keep the compiler's own format, so tools that read them (`file:line: error:` or MSVC `file(line): error C…`) keep working. `trusscli build` no longer passes the child's terminal through, so compiler colors are off in both modes; the log has the plain text.

**Web Build (WASM):**
The Web preset uses Ninja on Windows and Unix Makefiles on macOS/Linux,
matching the native presets. On Windows, trusscli automatically uses the
Ninja bundled with the detected Visual Studio installation (the **C++ CMake
tools for Windows** component). Otherwise, `ninja` must be on `PATH`.
The Web preset pins only the Ninja executable; Emscripten supplies the
compiler, and no Visual Studio developer shell is needed. Do not assume
the Emscripten SDK provides Ninja.

Before `trusscli update --web` or a manual Web build, activate the SDK in
the terminal you will build from: run `emsdk_env.bat` in cmd (its environment
persists in that cmd session), `./emsdk_env.ps1` in PowerShell, or
`source ./emsdk_env.sh` in Git Bash, from the SDK directory. With `MSYSTEM`
inherited from Git Bash, `emsdk_env.bat` only prints Bash exports; use the
shell script in Git Bash.

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
- JDK (`JAVA_HOME` environment variable) — javac/jar for core and app Java, d8 from SDK build-tools, and APK signing. CMake looks for javac and jar in `$JAVA_HOME/bin` as well as its normal program paths.

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
- Touch input: On Android, touch events are delivered via `touchPressed()`/`touchMoved()`/`touchReleased()`. Touch-as-mouse is ON by default: the first finger down also drives the mouse (one `mousePressed`, then `mouseDragged`, then one `mouseReleased` when it lifts); other fingers only produce touch events. Call `setTouchAsMouse(false)` in `setup()` to receive touch events only.
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

## Android background media playback

Android background media playback can opt in to TrussC's mediaPlayback foreground service. The core Java service is compiled into every Android APK through javac/d8, so `hasCode` is always true. Nothing starts until the app opts in. Android 12+ forbids background service starts; start in setup() or another visible callback:

```cpp
void tcApp::setup() {
    setAndroidForegroundService(true);
}
```

To advance tracks from update() without a visible surface, enable hidden updates too. Without the foreground service Android may freeze or kill the background process:

```cpp
void tcApp::setup() {
    setAndroidForegroundService(true);
    setUpdateWhileHidden(true);
}
```

While hidden on mobile, update() must not create/update textures, Fbos, images or fonts, or trigger GPU flushes. Check visibility before GPU work:

```cpp
void tcApp::update() {
    if (isWindowOccluded()) return;
}
```

Android hidden ticks also deliver queued `runOnMainThread()` work, external `Deliver::Main` events and `SoundBuffer::loadAsync()` completions. These callbacks follow the same GPU restriction and visibility check. Image/Font/Video completions, other resource callbacks and GPU readback polling wait for a visible host turn. TrussC has no API for background GPU work.

Android 13+ requires runtime POST_NOTIFICATIONS permission for the notification drawer. Visible `setAndroidForegroundService(true)` requests it if needed, at most once per Activity instance. The service starts without waiting for the result; denial or dismissal leaves it running with its notification hidden from the drawer. Android < 13, false, and hidden calls request nothing. Opt in from a visible callback:

```cpp
void tcApp::setup() {
    setAndroidForegroundService(true);
}
```

A custom manifest must declare POST_NOTIFICATIONS; otherwise the system request has no effect. Add this inside `<manifest>`:

```xml
<uses-permission android:name="android.permission.POST_NOTIFICATIONS" />
```

With permission, a low-importance notification channel and ongoing notification appear. No wake lock is acquired: active audio normally keeps the CPU awake, while CPU-only ticks with the screen off need device verification. TrussC has no background wake-lock API.

Stop the service when playback ends:

```cpp
void tcApp::cleanup() {
    setAndroidForegroundService(false);
}
```

For targetSdk 34+, Google Play requires justification for every declared foreground service type in Play Console. For a store build that does not use background playback, copy `core/resources/android/AndroidManifest.xml.in` into the project's `android/AndroidManifest.xml.in`, then remove these entries (keep hasCode true, since core Java is still packaged):

```xml
<uses-permission android:name="android.permission.FOREGROUND_SERVICE"/>
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK"/>
<uses-permission android:name="android.permission.POST_NOTIFICATIONS"/>
<!-- Remove this entire service element from application: -->
<service android:name="org.trussc.core.ForegroundService"
    android:foregroundServiceType="mediaPlayback" android:exported="false"/>
```

The app should leave the service stopped in that build:

```cpp
void tcApp::setup() {
    setAndroidForegroundService(false);
}
```

iOS hidden updates are not implemented. Keep `setUpdateWhileHidden(false)` and use Sound/AudioEngine's audio thread for playback; audio background mode must be configured in the app's Info.plist. TrussC has no iOS background update API:

```cpp
void tcApp::setup() {
    setUpdateWhileHidden(false);
}
```

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
`build_all.py` uses `CMAKE_BUILD_PARALLEL_LEVEL` when it is a positive integer; otherwise, it uses the CPU count.

`examples/build_all.py` is the Python version of it, and it also builds and runs the
core and addon tests (`--core-tests-only`, `--addon-tests-only`). It prints a
short screen by default: a dot line for progress, compiler warnings and errors,
`FAILED` lines with their details, per-suite counts like
`allCoreTests: 141/141 passed`, and a last line that starts with `Done:` or
`Failed:` (then `ERROR:` / `WARNING:` / `FAIL:` lines with counts, only when
they are not zero). The full output of each build and test goes to its own file
in `build-logs/` at the repository root (for example
`build-logs/core/tests/foo.log`), overwritten on each run. A test that crashes,
times out or fails without a `FAIL` line prints the last lines of its output and
the path of its log. Pass `--verbose` to print everything on screen; CI does.

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
On Apple platforms, the first use chooses one existing folder relative to the
executable: `data` (iOS), then `../Resources/data` (macOS release bundle), then
`../../../data` (macOS development). An explicit `setDataPathRoot()` overrides
this probe. The selected folder stays fixed for the process; a missing file
never falls back to an asset outside the bundle. `setDataPathToResources()`
continues to explicitly select `Contents/Resources/data`.

The reusable [release workflow](../../.github/workflows/release.yml) copies
`bin/data` into `Contents/Resources/data` before signing a macOS app, and beside
the executable on Windows and Linux. Normal macOS builds do not copy data into
the bundle, so development continues to use `bin/data`.

### Release Packages

App repositories can call `.github/workflows/release.yml`. `trussc-ref` takes
priority over the app's `.trussc-version`; without either, the workflow selects
the latest GitHub release from `trussc-repository`. The actual ref, tag and commit
for each platform are recorded in the release notes. This default follows new
TrussC releases; pin a tag or commit when a rebuild must use a fixed version.

Packages build with `RelWithDebInfo`, reject hot reload, and use `macos-15`,
`windows-2025` and `ubuntu-24.04` runner images. macOS packages are arm64 only,
with a deployment target and `LSMinimumSystemVersion` of 14.0. Signing uses
hardened runtime, timestamps, and camera/microphone entitlements by default;
`entitlements` replaces the defaults with an app-specific plist relative to
`project-path`. Nested code is signed before the app, followed by the DMG.
Data inside the signed bundle is part of the signature; changing it invalidates
the signature.

The Windows zip uses the dynamic MSVC runtime (`/MD`). Target machines need the
Microsoft Visual C++ 2015–2022 Redistributable (x64) installed separately; the
zip does not install or bundle it.

Matching PDB, dSYM and Linux debug files are uploaded as separate symbol
artifacts and release assets, outside the user packages. Keep them for crash
analysis. On Linux the packaged executable contains a GNU debug link; extract
the matching `.debug` file beside it (or into a `.debug` subdirectory) for GDB.

`publish: false` builds and verifies packages without creating a GitHub Release.
`project-path` defaults to `.`; the weekly package check uses `cursorExample`
with its data folder and ad-hoc signing (`sign-identity: '-'`). Ad-hoc signing
skips certificate import, notarization and stapling, and uses no timestamp.

### App Icon
Place icon files in the `icon/` folder. For PNG input, use a 1024×1024 square image; smaller PNGs are scaled up to 1024 on macOS/iOS and look blurry:

- **macOS:**
  - `.icns` - Traditional icon format (required for older macOS)
  - `.icon` - New format for macOS 26 Tahoe+ (created with Icon Composer, requires Xcode)
  - Both can coexist for compatibility across macOS versions
  - Otherwise, a `.png` is converted at configure time with `sips` + `iconutil` (no full Xcode required); missing or failing tools produce a warning and use the default TrussC icon. A non-square PNG is stretched to a square, with a warning. To test the fallback, configure with `-DTRUSSC_SIPS_EXECUTABLE=NOTFOUND` (or `TRUSSC_ICONUTIL_EXECUTABLE`).
  - An empty `icon/` uses the default icon without a warning.
- **iOS:** A `.png` is resized to 1024×1024 at configure time with `sips`, inside the build tree. Without a PNG, the 1024×1024 TrussC default is used; missing or failing `sips` warns and uses that default.
- **Windows:**
  - `.ico` - Windows icon format; a user file in `icon/` takes priority over PNG
  - `.png` - Converted at configure time with ImageMagick 7 (`magick`) to `<target>_icon.ico` in the build tree; nothing is written into `icon/`. The result is reused while the PNG's SHA-256 is unchanged; PNG edits trigger reconfiguration and conversion. A non-square PNG produces a warning. Missing or failing ImageMagick warns and uses `default.ico`.
- **Linux:** The first `.png` in glob order is embedded in the executable at build time and used for every X11 window (also under XWayland). Without a PNG, the default TrussC icon is embedded. PNG edits, additions, and removals trigger reconfiguration. No image conversion tool is needed. 1024 px is recommended; larger images are automatically scaled down to a 1024 px longest side, preserving aspect ratio.

### Windows Application Manifest (UTF-8)
Every Windows executable built through `trussc_app()` embeds an application manifest (`core/resources/windows/app.manifest`), merged with the linker's default one:

- `activeCodePage` = `UTF-8`: on Windows 10 version 1903 or later the process code page is UTF-8, so narrow strings are UTF-8 in every API that takes them (`fs::path(std::string)`, `path.string()`, `fopen`, `getenv`, the `-A` Win32 functions), in TrussC, addons and third-party libraries alike. A UTF-8 literal or `std::string` works as a file path: `img.load("写真.png")`. Older Windows ignores the setting. There, and in an executable not built through `trussc_app()`, narrow strings are in the system code page (CP932, CP1252): convert a UTF-8 string with `utf8ToPath()` before passing it to `fs::path`, `load()` or `save()`. That includes the UTF-8 strings the path helpers return (`getFileName()`, `joinPath()`, `listDirectory()`, ...).
- `longPathAware`: paths longer than 260 characters work where Windows has long paths enabled (the `LongPathsEnabled` policy).

The console output code page is UTF-8 as well: `runApp()` (through `sapp_desc.win32.console_utf8`) and `runHeadlessApp()` switch it to UTF-8 while the app runs, so UTF-8 log text reads correctly in the console of a Debug or `TRUSSC_SHOW_CONSOLE` build. This is done at run time, not by the manifest. The previous code page comes back when `runApp()` / `runHeadlessApp()` returns, and when Ctrl+C or Ctrl+Break ends the app. In a headless app, the first Ctrl+C or Ctrl+Break records a pending signal; the loop stops between update steps and normal teardown restores the code page. A second one while that signal is pending restores the code page and falls through to the default handler to end a hung app, including during cleanup. `std::exit()`, `abort()`, an uncaught exception or a crash leave the console in UTF-8; `chcp` with the old number (e.g. `chcp 932`) sets it back.

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

Generated shader headers reach consumers through the `<name>_shaders` INTERFACE target, which carries both their include directory and generation dependency, including for shader-less apps. `TrussC` links `TrussC_shaders` PUBLIC; hot reload host and guest explicitly link the core, app and loaded addon shader targets. After calling `trussc_compile_shaders(...)`, a new addon links its shader target with `target_link_libraries(${ADDON_NAME} PUBLIC ${ADDON_NAME}_shaders)` (use `INTERFACE` for a header-only addon). Headers remain in their source-tree locations; the separate docs bootstrap builds `TrussC_shaders_gen`. With Ninja and an MSVC-style compiler (Windows), `trussc_shader_object_depends()` also lists the generated headers as object dependencies of the consuming sources, so an edited shader recompiles them in the same build; core, apps and the addon loader call it for you. It does not cover a precompiled header, so a core shader header reached only through the PCH can still need a second build there.

Shader files using compute features (dispatch or storage buffers) use the suffix `*.compute.glsl` under `src/`, whether they contain `@cs`, `@vs`/`@fs`, or both. CMake rejects storage-buffer declarations in plain `.glsl` files and reports the required `.compute.glsl` filename. They have a separate sokol-shdc target set with `glsl310es` for GLES compute; ordinary `.glsl` graphics sources keep `glsl300es` for WebGL2. Include the resulting `*.compute.glsl.h` and pass its descriptor function to `ComputeShader::load()` or `Shader::load()` for its program. Both 300ES graphics and 310ES compute require a GLES 3.1 or later context when used together.

### macOS Deployment Target

TrussC requires **macOS 14.0 (Sonoma)** or later. This is because sokol's display backend uses `CADisplayLink` and `CAFrameRateRange` (introduced in macOS 14.0) for proper frame rate control, including ProMotion 120Hz support.

The Project Generator automatically sets `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` in both the `macos` and `xcode` presets.

### TrussC build types

TrussC defaults to **RelWithDebInfo** (Release with Debug Info). This provides optimized performance (`-O2`) while keeping debug symbols for stack traces and breakpoint debugging. We believe this is the best default for creative coding — you get near-Release speed without losing the ability to debug.

`trusscli build` and `trusscli run` remember the last explicit `--debug`,
`--release`, or `--relwithdebinfo` flag for each native project. Without a flag,
they reuse that type, or use RelWithDebInfo when none is saved. Each build
prints its type, with `(remembered)` when reusing the saved selection.

```bash
trusscli build --release
trusscli run
```

The machine-local choice lives in `CMakePresets.json` under
`vendor.trussc.buildType`, alongside the `use-local` marker, and survives
`trusscli update` and `trusscli clean`. Web uses MinSizeRel and Android uses
Release independently of this choice. `dist` selects its own build type and
leaves the saved choice unchanged. There is no tcApp API for this CLI setting.

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

### Bundle Identifier (macOS / iOS)

On macOS and iOS the app's bundle identifier defaults to `com.trussc.<project name>`. Set your own one in `local.cmake` before you distribute the app, e.g. `set(TC_BUNDLE_ID "com.example.myApp")`. `trussc_app()` reads `TC_BUNDLE_ID` after including `local.cmake` and uses it for `CFBundleIdentifier` in the generated Info.plist and for the Xcode `PRODUCT_BUNDLE_IDENTIFIER` setting, so it survives `trusscli update`. macOS keys privacy permissions and user defaults by this identifier, so two apps with the same project name and the default identifier share them. Other platforms ignore `TC_BUNDLE_ID`.

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

A normal Host build also builds the Guest. A Guest-only reload never builds or
relinks the Host, even when `main.cpp` includes app headers or generated shader
headers that changed. On Windows, the Guest uses a separate import library
generated from the core export definitions, without depending on the Host target.
Under Windows hot reload the Guest imports only TrussC core symbols; app code
shared between Host and Guest goes in the Guest or in an addon.
On Windows, a failed guest build prints a visibility reminder: functions in `main.cpp` and host-only libraries must be moved or linked into the guest to be usable there.

The Guest is built and loaded in the Host's configuration. With multi-config
generators (Xcode, Visual Studio, Ninja Multi-Config), rebuilds pass
`--config <host configuration>` and load the library from
`<buildDir>/<host configuration>/`. Single-config generators (Ninja, Makefiles)
keep the library directly in `<buildDir>` and use the configured build type.

The Host monitors `src/` for file modifications (polling every 500ms). When a change is detected:
1. Guest is rebuilt via `cmake --build --target guest` (incremental — only your code, not TrussC core)
2. The old Guest's App is ended and destroyed (`events().hotReloadUnload` fires first): its `exit()` runs, its `audioOut()` / `audioIn()` are detached (waiting for a call in flight), then its `cleanup()` runs. Its library stays loaded: host-owned state can still point into its code, so it is never `dlclose`d / `FreeLibrary`d
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

- **Incompatible code**: addon headers and user code can declare a hot-reload limitation at namespace scope:
  ```cpp
  TC_NO_HOT_RELOAD("this code keeps state that cannot survive reloads");
  ```
  A hot-reload guest fails to compile with `hot reload is not supported here:` followed by the reason. The macro expands to nothing in ordinary builds and in the host, even when `main.cpp` includes the same header. Remove `TC_HOT_RELOAD` to build the app normally. `tcxHap` uses this macro on Windows because its snappy dependency is not linked into the guest.
- **Supported platforms**: macOS (`.dylib`), Linux (`.so`), Windows (`.dll`). Wasm / iOS / Android fall back to static mode automatically.
- **Windows guest state**: the guest DLL compiles its own copy of every header-inline variable, so framework state that host and app code share lives in the host behind non-inline functions ([Architecture §5.G](../architecture/README.md#g-one-instance-per-process-header-inline-state)): MCP tools, events, timers, audio, recording, the main-thread queue, `setFps()` / `redraw()`, the clip / fov defaults, touch-as-mouse, the data path root, bitmap-font glyphs, the overlay (tcxImGui) queries, the debug counters behind `getNodeCount()` / `getTextureCount()` / `getFboCount()`, the current window context and the secondary windows' double-attach guard (so guest code sees the host's release when a window closes; a closed App is not attached again, so after that you attach a new App) all reach the host from a Windows guest too, and guest code sees the host's `WindowSettings::pixelPerfect` and sokol_gl budget. The GPU caches (FBO contexts and pipelines, IBL bake pipelines, font atlases and samplers) are the host's as well, so a reload reuses them instead of filling the host's sokol pools with a new set each time. What each module still keeps for itself is listed in `tools/header_state_allowlist.txt` with the reason it is harmless: small derived caches (demangled type names). Warn-once gates (`static OnceGate`) are per module too; after a reload a warning can show once more. Addons' own header-inline state is the guest's by design.
- **Comment style**: Use `//` to disable. `/* */` block comments are not detected by the cmake scanner.
- **Build tool**: `trusscli build` handles hot reload state changes in one step. Raw `cmake --build` may require building twice when toggling `TC_HOT_RELOAD` on/off.
- **Build errors**: If the code doesn't compile, the previous version keeps running. Fix the error and save again.
