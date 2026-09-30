#pragma once

// Decisions `trusscli build` makes before it runs `cmake --build` (#357).
// They are pure functions of their inputs, so the test in
// core/tests/trusscliPresets can check them with fake inputs.
//
//   - planConfigure(): whether a configure pass (`cmake --preset <target>`)
//     has to run first, with which -D options, and what to print. A missing
//     CMakeCache.txt needs one: `cmake --build` does not configure by itself.
//   - checkPresetToolchain(): on Windows, the "windows" configure preset
//     pins the ninja, MSVC and Windows SDK paths found when it was written.
//     After a Visual Studio update or a move to another VS version those
//     paths no longer exist. `trusscli doctor` reports this, and
//     `trusscli build` re-pins them in CMakePresets.json (see main.cpp).

#include "VsDetector.h"

#include <functional>
#include <string>
#include <vector>

// What cmdBuild knows about the target's build folder and its own flags.
struct ConfigureInputs {
    std::string buildDir;             // folder name for messages, e.g. "build-linux"
    bool isNative = false;            // the native preset (macos / windows / linux)
    bool hasCache = false;            // <buildDir>/CMakeCache.txt exists
    bool generated = false;           // a configure finished there: the build system
                                      // (Makefile, build.ninja, *.xcodeproj, ...) exists
    std::string cachedBuildType;      // CMAKE_BUILD_TYPE in that cache ("" if none)
    std::string requestedBuildType;   // from --debug / --release / --relwithdebinfo ("" if none)
    bool warnings = false;            // --warnings
};

struct ConfigurePlan {
    bool configure = false;              // run `cmake --preset <target>` first
    std::vector<std::string> defines;    // extra arguments for it, e.g. "-DCMAKE_BUILD_TYPE=Release"
    std::vector<std::string> messages;   // lines to print, in order
};

// Decide the configure pass. A build with a cache that already holds what
// was asked for stays configure-free. A cache without a build system (a
// configure that failed) is configured again.
ConfigurePlan planConfigure(const ConfigureInputs& in);

// Fill the folder part of ConfigureInputs (buildDir, isNative, hasCache,
// generated, cachedBuildType) from <projectDir>/<the target preset's build
// folder> on disk. nativePreset is "" when the platform has none.
ConfigureInputs inspectBuildFolder(const std::string& projectDir,
                                   const std::string& targetPreset,
                                   const std::string& nativePreset);

// The build folders `trusscli clean` removes (when they exist): the native
// preset's folder and a plain "build", or with --all every preset's folder
// (xcode-ios for ios) and "build".
std::vector<std::string> buildFoldersToClean(const std::string& nativePreset, bool all);

// Pinned toolchain paths in a CMakePresets.json.
struct ToolchainCheck {
    bool pinned = false;                 // the "windows" preset pins at least one path
    std::vector<std::string> missing;    // pinned paths that do not exist (no duplicates)
    bool stale() const { return pinned && !missing.empty(); }
};

// Check every path the "windows" configure preset pins: CMAKE_MAKE_PROGRAM
// and each entry of the INCLUDE, LIB and PATH environment values. Entries
// that are CMake macros ($penv{PATH}, $env{...}, ${...}) are skipped.
// A preset without an environment block, and text that is not a presets
// file, give pinned == false. `exists` tells whether a path exists (a fake
// in the test, the filesystem in trusscli).
ToolchainCheck checkPresetToolchain(const std::string& presetsJson,
                                    const std::function<bool(const std::string&)>& exists);

// The toolchain paths the "windows" configure preset pins for one Visual
// Studio install. ProjectGenerator writes them, and `trusscli build` puts
// fresh ones into an existing CMakePresets.json.
struct WindowsToolchainPins {
    std::string makeProgram;   // CMAKE_MAKE_PROGRAM ("" = not pinned)
    std::string include;       // environment INCLUDE / LIB / PATH
    std::string lib;           // (all "" = no environment block)
    std::string path;
};
WindowsToolchainPins windowsToolchainPins(const VsVersionInfo& vs);

// Whether a detected Visual Studio entry has what the pins need (MSVC and
// Windows SDK versions). VsDetector's fallback entry, returned when no
// Visual Studio was found, does not.
bool canPinToolchain(const VsVersionInfo& vs);

// presetsJson with the "windows" configure preset's CMAKE_MAKE_PROGRAM and
// INCLUDE / LIB / PATH environment replaced by `pins` (a pin that is "" is
// removed). Everything else in the file (other presets, TRUSSC_DIR, the
// remembered IDE, hand edits) stays. "" when the text is not a presets file
// or has no "windows" configure preset.
std::string repinWindowsToolchain(const std::string& presetsJson,
                                  const WindowsToolchainPins& pins);

// Whether `trusscli build` refreshes CMakePresets.json before building:
// only for the native Windows preset, and only when its pinned toolchain is
// stale. Cross targets (web, android) do not use the pinned environment.
bool shouldRefreshPresets(const std::string& nativePreset,
                          const std::string& targetPreset,
                          const ToolchainCheck& check);
