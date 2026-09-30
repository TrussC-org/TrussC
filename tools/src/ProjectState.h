#pragma once

// Reading a project's generation options back from its CMakePresets.json.
//
// `update`, `addon add` and `addon remove` regenerate the build files. They
// start from what the project was generated with (IDE, web / android / ios
// targets, web backend), read from the project's own CMakePresets.json, and
// only then apply the flags given on the command line. Without a
// CMakePresets.json (fresh clone: the file is gitignored), the defaults of
// ProjectSettings apply (VSCode, native only, WebGPU).
//
// The targets and the web backend are the presets themselves ("web" /
// "android" / "ios" configure presets, TC_WEB_BACKEND of the web preset). The
// IDE is stored by ProjectGenerator::writeCMakePresets() as
//   "vendor": {"trussc": {"ide": "<id>"}}
// with the ids of IdeHelper::getIdeId().
//
// Whatever cannot be used is left out and reported, never guessed: a file
// that does not parse, wrongly typed entries, an unknown IDE id, an IDE this
// OS cannot generate (xcode off macOS, vs off Windows, as in the GUI), and an
// ios target off macOS (writeCMakePresets() writes it on macOS only).
// A TC_WEB_BACKEND other than "WGPU" / "GLES3" is read the way CMake builds
// it (GLES3) and reported too.
//
// The toolchainFile of a saved web / android preset is read as well, so a
// regeneration from a shell without emsdk_env / the NDK variables keeps a
// toolchain path that still exists (see chooseToolchainFile()). A target
// kept from the presets whose configure fails is a warning, not an error
// (ProjectSettings::webKept etc.).

#include "ProjectGenerator.h"
#include <optional>
#include <string>
#include <vector>

// What an existing CMakePresets.json says about how the project was generated.
struct PresetState {
    bool found = false;     // the file exists and parses as a JSON object
    bool web = false;       // a "web" configure preset exists
    bool android = false;   // an "android" configure preset exists
    bool ios = false;       // an "ios" configure preset exists (macOS only;
                            // elsewhere it is dropped with a warning)
    // TC_WEB_BACKEND of the web preset, as CMake builds it: 0 = WGPU (unset or
    // exactly "WGPU"), 1 = GLES3 (any other value; not "GLES3" is warned).
    // Read from a string or from the {"type": ..., "value": ...} form.
    int webBackend = 0;
    // toolchainFile of the web / android preset, as written (may be a
    // $env{...} form); empty when there is none.
    std::string webToolchainFile;
    std::string androidToolchainFile;
    bool hasIde = false;    // the vendor entry holds an IDE id usable on this OS
    IdeType ide = IdeType::VSCode;

    // Why a saved IDE was not used (empty when there was none or it was used):
    // a wrongly typed vendor entry, an unknown id, or an IDE this OS cannot
    // generate. hasIde is false whenever this is set.
    std::string ideWarning;
    // Other problems with an existing file: the whole file when it does not
    // parse as a JSON object (found stays false), a wrongly typed
    // configurePresets or toolchainFile (ignored), a TC_WEB_BACKEND that is
    // unusable (ignored: WGPU) or neither "WGPU" nor "GLES3" (GLES3), and an
    // ios preset off macOS (dropped).
    std::vector<std::string> warnings;
};

// Parse the text of a CMakePresets.json. Text that is not a JSON object
// gives found == false and a warning.
PresetState parsePresetState(const std::string& jsonText);

// Read <projectPath>/CMakePresets.json. A missing file gives found == false
// and no warning; an unreadable or unparsable one gives found == false and a
// warning.
PresetState readPresetState(const std::string& projectPath);

// Generation options given explicitly on the command line. An unset value
// keeps what the project already has.
struct GenerationFlags {
    std::optional<bool> web;
    std::optional<bool> android;
    std::optional<bool> ios;
    std::optional<IdeType> ide;
};

// Handle one target argument: --web / --android / --ios turn a target on,
// --no-web / --no-android / --no-ios turn it off. Returns false if `arg` is
// none of them. Returns true with a non-empty errMsg when it contradicts an
// earlier argument (e.g. --web and --no-web together).
bool parseTargetFlag(const std::string& arg, GenerationFlags& flags, std::string& errMsg);

// Set the IDE, the targets and the web backend of `settings`: first from the
// project's presets (when found), then from the explicit flags, which win.
// Also carries the saved toolchain files over, and marks the targets that
// come from the presets without a flag of this run (webKept etc.).
void applyGenerationOptions(ProjectSettings& settings,
                            const PresetState& state,
                            const GenerationFlags& flags);

// The settings a regeneration runs with, and what to tell the user.
struct RegenerationSetup {
    ProjectSettings settings;
    // Saved settings that could not be used, one line each, without
    // "Warning: " (for stderr).
    std::vector<std::string> warnings;
    // "Project settings: ..." naming what is kept (for stdout); empty when
    // the project has no usable CMakePresets.json.
    std::string summary;
};

// The shared setup of `trusscli update`, `addon add` and `addon remove`:
// ProjectSettings for the project at `projectPath` with the given addons,
// its CMakePresets.json read back (readPresetState) and the flags applied on
// top (applyGenerationOptions), plus the warnings and the summary line.
RegenerationSetup prepareRegeneration(const std::string& projectPath,
                                      const std::string& tcRoot,
                                      const std::vector<std::string>& addons,
                                      const std::vector<int>& addonSelected,
                                      const GenerationFlags& flags);

// One line naming the IDE and the targets of `settings`, for the log, e.g.
// "IDE cursor, targets: native, web (WebGPU)". ios is named on macOS only,
// the only host that writes its preset.
std::string describeGenerationOptions(const ProjectSettings& settings);
