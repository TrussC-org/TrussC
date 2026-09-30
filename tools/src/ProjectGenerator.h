#pragma once

#include "IdeHelper.h"
#include "VsDetector.h"
#include <string>
#include <vector>
#include <functional>

// Project generation settings
struct ProjectSettings {
    std::string projectName;
    std::string projectDir;
    std::string tcRoot;
    std::string templatePath;
    std::vector<std::string> addons;
    std::vector<int> addonSelected;
    IdeType ideType = IdeType::VSCode;
    bool generateWebBuild = false;
    bool generateAndroidBuild = false;
    bool generateIosBuild = false;
    int webBackend = 0;  // 0: WebGPU, 1: WebGL
    int selectedVsIndex = 0;
    std::vector<VsVersionInfo> installedVsVersions;

    // Set from an existing CMakePresets.json by applyGenerationOptions()
    // (update / addon add / addon remove, see ProjectState.h). `trusscli new`
    // and the GUI leave them empty / false.
    //
    // toolchainFile of the saved web / android preset. Used again when this
    // shell cannot find the toolchain (no emsdk_env, no ANDROID_NDK_HOME) and
    // the saved file still exists; see chooseToolchainFile().
    std::string savedWebToolchainFile;
    std::string savedAndroidToolchainFile;
    // The target comes from the saved presets, not from a flag of this run.
    // Its configure failing is then a warning, not an error: the other build
    // files are still regenerated (see ProjectGenerator::getWarnings()).
    bool webKept = false;
    bool androidKept = false;
    bool iosKept = false;

    // プラットフォーム固有のビルド環境を検出して設定
    // CLI/GUI共通で呼ぶこと
    void detectBuildEnvironment() {
#ifdef _WIN32
        installedVsVersions = VsDetector::detectInstalledVersions();
        if (!installedVsVersions.empty()) {
            selectedVsIndex = 0; // 最新バージョンを使用
        }
#endif
    }
};

// The toolchainFile to write into a web / android preset. `detected` is what
// this shell finds (empty when nothing was found), `saved` the toolchainFile
// of the existing preset, `envFallback` the $env{...} form CMake resolves at
// configure time. A found toolchain wins; otherwise a saved absolute path
// that still exists is kept; otherwise the $env{...} form.
std::string chooseToolchainFile(const std::string& detected,
                                const std::string& saved,
                                const std::string& envFallback);

// Project generator class
class ProjectGenerator {
public:
    using LogCallback = std::function<void(const std::string&)>;

    explicit ProjectGenerator(const ProjectSettings& settings);

    // Set log callback for progress messages
    void setLogCallback(LogCallback callback) { logCallback_ = callback; }

    // Generate new project
    // Returns empty string on success, error message on failure
    std::string generate();

    // Update existing project
    std::string update(const std::string& projectPath);

    // Write only CMakePresets.json into an existing project, from the current
    // settings (no addons.make, CMakeLists.txt, IDE files or configure).
    // Returns empty string on success, error message on failure.
    std::string writePresets(const std::string& projectPath);

    // Get destination path
    std::string getDestPath() const;

    // Build folder of a configure preset, relative to the project:
    // "ios" -> "xcode-ios", any other preset -> "build-<preset>". The one
    // mapping for the presets written here, `trusscli build` and
    // `trusscli clean`.
    static std::string buildDirForPreset(const std::string& preset);

    // Build scripts trusscli writes next to the project for a configure
    // preset, relative to the project: for "web" the one of each host OS
    // (build-web.bat / build-web.command / build-web.sh, see
    // generateWebBuildFiles()), none for the other presets.
    static const std::vector<std::string>& buildScriptsForPreset(const std::string& preset);

    // Every configure preset trusscli can write:
    // macos, linux, windows, web, android, ios
    static const std::vector<std::string>& allPresetNames();

    // Problems of the last generate() / update() that did not fail it: a
    // target kept from the saved presets (ProjectSettings::webKept etc.)
    // whose configure failed. One message per problem, without "Warning: ".
    const std::vector<std::string>& getWarnings() const { return warnings_; }

private:
    ProjectSettings settings_;
    LogCallback logCallback_;
    std::vector<std::string> warnings_;

    void log(const std::string& msg);

    // Get TRUSSC_DIR value (relative or absolute path)
    std::string getTrusscDirValue(const std::string& projectPath);

    // Generate IDE-specific files
    void generateVSCodeFiles(const std::string& path);
    void generateXcodeProject(const std::string& path);
    void generateVisualStudioProject(const std::string& path);
    void generateWebBuildFiles(const std::string& path);

    // Run CMake configure for VSCode/Cursor (generates compile_commands.json)
    void runCMakeConfigure(const std::string& path);

    // Write addons.make
    void writeAddonsMake(const std::string& destPath);

    // Clean build directories for current platform
    void cleanBuildDirectories(const std::string& path);

    // Write CMakePresets.json (OS-specific preset with TRUSSC_DIR)
    // DESIGN NOTE: All project-specific configuration goes into CMakePresets.json
    // CMakeLists.txt is copied as-is from template (no modifications)
    // The IDE is stored there too, as "vendor": {"trussc": {"ide": "<id>"}},
    // so update / addon add / addon remove can keep it (see ProjectState.h).
    void writeCMakePresets(const std::string& destPath);

    // Run cmake --preset for enabled cross-compile targets (iOS, Android, Web).
    // Returns false if any preset failed to configure, so callers can fail
    // the whole generate/update instead of silently leaving no build files.
    // A target kept from the saved presets that fails is a warning instead
    // (getWarnings()), and does not make it return false.
    bool runCrossCompilePresets(const std::string& path);
};
