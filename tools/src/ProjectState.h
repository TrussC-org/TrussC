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

#include "ProjectGenerator.h"
#include <optional>
#include <string>

// What an existing CMakePresets.json says about how the project was generated.
struct PresetState {
    bool found = false;     // the file exists and parses as a JSON object
    bool web = false;       // a "web" configure preset exists
    bool android = false;   // an "android" configure preset exists
    bool ios = false;       // an "ios" configure preset exists
    int webBackend = 0;     // TC_WEB_BACKEND of the web preset: 0 = WGPU, 1 = GLES3
    bool hasIde = false;    // the vendor entry holds a known IDE id
    IdeType ide = IdeType::VSCode;
};

// Parse the text of a CMakePresets.json. Text that is not a JSON object
// gives found == false.
PresetState parsePresetState(const std::string& jsonText);

// Read <projectPath>/CMakePresets.json. A missing or unreadable file gives
// found == false.
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
void applyGenerationOptions(ProjectSettings& settings,
                            const PresetState& state,
                            const GenerationFlags& flags);

// One line naming the IDE and the targets of `settings`, for the log, e.g.
// "IDE cursor, targets: native, web (WebGPU)".
std::string describeGenerationOptions(const ProjectSettings& settings);
