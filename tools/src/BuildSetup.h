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
//     `trusscli build` rewrites CMakePresets.json (see main.cpp).

#include <functional>
#include <string>
#include <vector>

// What cmdBuild knows about the target's build folder and its own flags.
struct ConfigureInputs {
    std::string buildDir;             // folder name for messages, e.g. "build-linux"
    bool isNative = false;            // the native preset (macos / windows / linux)
    bool hasCache = false;            // <buildDir>/CMakeCache.txt exists
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
// was asked for stays configure-free.
ConfigurePlan planConfigure(const ConfigureInputs& in);

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

// The string value of a cache variable of one configure preset ("" when the
// preset, the variable or the file is missing). Used to keep the project's
// TRUSSC_DIR when the presets are refreshed.
std::string presetCacheVariable(const std::string& presetsJson,
                                const std::string& preset,
                                const std::string& variable);

// Whether `trusscli build` refreshes CMakePresets.json before building:
// only for the native Windows preset, and only when its pinned toolchain is
// stale. Cross targets (web, android) do not use the pinned environment.
bool shouldRefreshPresets(const std::string& nativePreset,
                          const std::string& targetPreset,
                          const ToolchainCheck& check);
