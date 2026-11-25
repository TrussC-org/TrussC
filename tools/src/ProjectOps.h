#pragma once

#include "AddonVersions.h"
#include "ProjectState.h"
#include "OperationLog.h"
#include "ProcessCapture.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Project operations shared by command-line and graphical interfaces.
namespace projectOps {
// Diagnostic messages retain unprefixed stderr presentation in the CLI
// (registry progress/errors); graphical callers choose their own presentation.
using LogCallback = DiagnosticCallback;
using ConfigureOutputCallback = std::function<void(const std::string& projectPath,
    const std::string& buildDir, const std::string& output, int result)>;

struct UpdateInput {
    std::string projectPath;
    std::string tcRoot; // An explicit local override, saved like use-local.
    std::filesystem::path executablePath; // Root autodetection when no version is selected.
    GenerationFlags flags;
    bool fetchVersion = true;
    bool versionAcquired = false;
    bool requirePlainProject = false;
    // The GUI supplies its current selection and generation settings. Other
    // callers restore settings from the project and apply flags instead.
    std::optional<ProjectSettings> settings;
    addonVersions::Lookup addonLookup; // Optional registry lookup.
    ConfigureOutputCallback configureOutput;
};

struct UseInput {
    UpdateInput update; // Regeneration context and presentation callbacks.
    std::string spec;
    bool clear = false;
};

struct UseLocalInput {
    UpdateInput update;
    std::string path;
    bool clear = false;
};

struct BuildInput {
    UpdateInput update; // Root detection, offline regeneration and addon lookup.
    std::string targetPreset; // Empty selects the native desktop preset.
    std::string buildType;
    bool clean = false;
    bool mcp = false;
    bool warnings = false;
    bool dryRun = false;
    bool verbose = false;
    bool rememberType = true;
    int jobs = 0;
    CancelHandle* cancel = nullptr;
    bool forwardSignals = false; // CLI only, never enabled by a GUI worker.
};

struct Result {
    bool success = false;
    bool stopped = false;
    int exitCode = 1;
    int forwardedSignal = 0;
    std::string failureReason;
    // Original final error when a separately logged spawn diagnostic enriches
    // failureReason. The CLI keeps its existing two-message presentation.
    std::string failureSummary;
    std::vector<std::string> warnings;
    std::vector<std::string> logLines;
};

Result use(const UseInput& input, const LogCallback& log = {});
Result useLocal(const UseLocalInput& input, const LogCallback& log = {});
Result update(const UpdateInput& input, const LogCallback& log = {});
Result build(const BuildInput& input, const LogCallback& log = {});
std::string findCMake();
std::string detectProjectRoot(const std::string& startPath = "");
std::string detectTcRoot(const std::filesystem::path& executablePath);
addonVersions::Paths resolveAddons(const std::string& project, const std::string& root,
    bool fetch, const addonVersions::Lookup& lookup = {},
    const std::vector<std::string>* selection = nullptr);
}
