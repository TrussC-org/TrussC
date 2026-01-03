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
    int selectedVsIndex = 0;
    std::vector<VsVersionInfo> installedVsVersions;
};

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

    // Get destination path
    std::string getDestPath() const;

private:
    ProjectSettings settings_;
    LogCallback logCallback_;

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
    void writeCMakePresets(const std::string& destPath);
};
