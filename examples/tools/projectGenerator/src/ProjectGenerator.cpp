#include "ProjectGenerator.h"
#include "TrussC.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>  // chmod
#endif

namespace fs = std::filesystem;
using namespace std;
using namespace tc;

// Execute command and capture output
static pair<int, string> executeCommand(const string& cmd) {
    string output;
    string fullCmd = cmd + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
    FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
    if (!pipe) return {-1, "Failed to execute command"};
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    int result = _pclose(pipe);
#else
    int result = pclose(pipe);
#endif
    return {result, output};
}

ProjectGenerator::ProjectGenerator(const ProjectSettings& settings)
    : settings_(settings)
{
}

void ProjectGenerator::log(const string& msg) {
    if (logCallback_) {
        logCallback_(msg);
    }
}

string ProjectGenerator::getDestPath() const {
    string dir = settings_.projectDir;
    while (!dir.empty() && dir.back() == '/') {
        dir.pop_back();
    }
    return dir + "/" + settings_.projectName;
}

string ProjectGenerator::getTrusscDirValue(const string& projectPath) {
    fs::path project = fs::absolute(projectPath);
    fs::path trussc = fs::absolute(settings_.tcRoot) / "trussc";

    // Calculate relative path
    fs::path rel = fs::relative(trussc, project);

    // Use relative path if it exists, otherwise use absolute
    if (fs::exists(project / rel)) {
        string result = "${CMAKE_CURRENT_SOURCE_DIR}/" + rel.string();
        // Normalize path separators
        for (char& c : result) {
            if (c == '\\') c = '/';
        }
        return result;
    }
    return trussc.string();
}

void ProjectGenerator::writeCMakeLists(const string& destPath) {
    string templateCmakePath = settings_.templatePath + "/CMakeLists.txt";
    ifstream inFile(templateCmakePath);
    stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    string content = buffer.str();

    // Replace TRUSSC_DIR
    size_t pos = content.find("set(TRUSSC_DIR \"");
    if (pos != string::npos) {
        size_t endPos = content.find("\")", pos);
        if (endPos != string::npos) {
            content.replace(pos, endPos - pos + 2,
                "set(TRUSSC_DIR \"" + getTrusscDirValue(destPath) + "\")");
        }
    }

    string cmakePath = destPath + "/CMakeLists.txt";
    ofstream outFile(cmakePath);
    outFile << content;
    outFile.close();
}

void ProjectGenerator::writeAddonsMake(const string& destPath) {
    string addonsMakePath = destPath + "/addons.make";
    ofstream addonsFile(addonsMakePath);
    addonsFile << "# TrussC addons - one addon per line\n";
    for (size_t i = 0; i < settings_.addons.size(); i++) {
        if (settings_.addonSelected[i]) {
            addonsFile << settings_.addons[i] << "\n";
        }
    }
    addonsFile.close();
}

void ProjectGenerator::writeCMakeUserPresets(const string& destPath) {
#ifdef _WIN32
    // Get ninja path from detected VS versions
    if (settings_.installedVsVersions.empty() ||
        settings_.selectedVsIndex >= (int)settings_.installedVsVersions.size()) {
        return;
    }

    string ninjaPath = settings_.installedVsVersions[settings_.selectedVsIndex].ninjaPath;
    if (ninjaPath.empty()) {
        return;
    }

    // Convert backslashes to forward slashes for JSON
    for (char& c : ninjaPath) {
        if (c == '\\') c = '/';
    }

    log("Writing CMakeUserPresets.json...");

    // Generate CMakeUserPresets.json with ninja path
    // Uses "default" preset name which inherits from "windows" and adds CMAKE_MAKE_PROGRAM
    Json presets;
    presets["version"] = 6;

    Json defaultPreset;
    defaultPreset["name"] = "default";
    defaultPreset["displayName"] = "Default (Windows)";
    defaultPreset["inherits"] = "windows";
    defaultPreset["cacheVariables"]["CMAKE_MAKE_PROGRAM"] = ninjaPath;

    presets["configurePresets"] = Json::array();
    presets["configurePresets"].push_back(defaultPreset);

    Json defaultBuildPreset;
    defaultBuildPreset["name"] = "default";
    defaultBuildPreset["configurePreset"] = "default";

    presets["buildPresets"] = Json::array();
    presets["buildPresets"].push_back(defaultBuildPreset);

    saveJson(presets, destPath + "/CMakeUserPresets.json");
#endif
}

string ProjectGenerator::generate() {
    // Validation
    if (settings_.projectName.empty()) {
        return "Project name is required";
    }
    if (settings_.projectDir.empty()) {
        return "Location is required";
    }
    if (settings_.tcRoot.empty()) {
        return "TrussC folder not set";
    }
    if (!fs::exists(settings_.templatePath)) {
        return "Template not found";
    }

    string destPath = getDestPath();
    bool folderExists = fs::is_directory(destPath);

    try {
        log("Creating project: " + settings_.projectName);

        // Create parent directory
        fs::create_directories(settings_.projectDir);

        // Copy template only if folder doesn't exist (new project)
        if (!folderExists) {
            log("Copying template files...");
            fs::create_directories(destPath);
            for (const auto& entry : fs::directory_iterator(settings_.templatePath)) {
                string name = entry.path().filename().string();
                // Skip build directories and output
                if (name == "build" || name == "bin" ||
                    name == "build-macos" || name == "build-windows" || name == "build-linux") {
                    continue;
                }
                fs::copy(entry.path(), destPath + "/" + name,
                         fs::copy_options::recursive);
            }
        }

        log("Writing CMakeLists.txt...");
        writeCMakeLists(destPath);

        log("Writing addons.make...");
        writeAddonsMake(destPath);

        // Generate IDE-specific files
        if (settings_.ideType == IdeType::VSCode || settings_.ideType == IdeType::Cursor) {
            log("Generating VSCode files...");
            generateVSCodeFiles(destPath);
            // Write CMakeUserPresets.json for Windows (contains ninja path)
            writeCMakeUserPresets(destPath);
            // Run CMake configure to generate compile_commands.json
            runCMakeConfigure(destPath);
        } else if (settings_.ideType == IdeType::Xcode) {
            log("Generating Xcode project...");
            generateXcodeProject(destPath);
        } else if (settings_.ideType == IdeType::VisualStudio) {
            log("Generating Visual Studio project...");
            generateVisualStudioProject(destPath);
        }

        if (settings_.generateWebBuild) {
            log("Generating Web build files...");
            generateWebBuildFiles(destPath);
        }

        log("Done!");
        return "";  // Success

    } catch (const exception& e) {
        return string("Error: ") + e.what();
    }
}

string ProjectGenerator::update(const string& projectPath) {
    try {
        log("Updating project...");

        // Update CMakeLists.txt
        log("Updating CMakeLists.txt...");
        writeCMakeLists(projectPath);

        // Update CMakePresets.json (always overwrite with latest template)
        string templatePresets = settings_.templatePath + "/CMakePresets.json";
        string destPresets = projectPath + "/CMakePresets.json";
        if (fs::exists(templatePresets)) {
            log("Updating CMakePresets.json...");
            fs::copy_file(templatePresets, destPresets, fs::copy_options::overwrite_existing);
        }

        // Update addons.make
        log("Updating addons.make...");
        writeAddonsMake(projectPath);

        // Regenerate IDE-specific files
        if (settings_.ideType == IdeType::VSCode || settings_.ideType == IdeType::Cursor) {
            log("Updating VSCode files...");
            generateVSCodeFiles(projectPath);
            // Write CMakeUserPresets.json for Windows (contains ninja path)
            writeCMakeUserPresets(projectPath);
            // Run CMake configure to generate compile_commands.json
            runCMakeConfigure(projectPath);
        } else if (settings_.ideType == IdeType::Xcode) {
            log("Regenerating Xcode project...");
            generateXcodeProject(projectPath);
        } else if (settings_.ideType == IdeType::VisualStudio) {
            log("Regenerating Visual Studio project...");
            generateVisualStudioProject(projectPath);
        }

        if (settings_.generateWebBuild) {
            log("Updating Web build files...");
            generateWebBuildFiles(projectPath);
        }

        log("Update complete!");
        return "";

    } catch (const exception& e) {
        return string("Error: ") + e.what();
    }
}

void ProjectGenerator::generateVSCodeFiles(const string& path) {
    string vscodePath = path + "/.vscode";
    fs::create_directories(vscodePath);

    // launch.json
    Json launch;
    launch["version"] = "0.2.0";
    launch["configurations"] = Json::array();

    Json config;
    config["name"] = "Debug";
    config["request"] = "launch";
    config["cwd"] = "${workspaceFolder}";
    config["preLaunchTask"] = "CMake: build";

    if (settings_.ideType == IdeType::Cursor) {
        // Cursor: C/C++ extension is blocked, use CodeLLDB for all platforms
        config["type"] = "lldb";
#ifdef __APPLE__
        config["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.app/Contents/MacOS/${workspaceFolderBasename}";
#elif defined(_WIN32)
        config["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.exe";
#else
        config["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}";
#endif
    } else {
        // VSCode: use platform-specific debuggers
        config["type"] = "lldb";

        Json osx;
        osx["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.app/Contents/MacOS/${workspaceFolderBasename}";
        config["osx"] = osx;

        Json linux_cfg;
        linux_cfg["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}";
        config["linux"] = linux_cfg;

        Json windows;
        windows["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.exe";
        windows["type"] = "cppvsdbg";
        config["windows"] = windows;
    }

    launch["configurations"].push_back(config);
    saveJson(launch, vscodePath + "/launch.json");

    // settings.json
    Json settings;
    settings["cmake.sourceDirectory"] = "${workspaceFolder}";
    settings["cmake.useCMakePresets"] = "always";
    settings["cmake.configureOnOpen"] = true;
    settings["cmake.buildBeforeRun"] = true;
#ifdef __APPLE__
    settings["cmake.buildDirectory"] = "${workspaceFolder}/build-macos";
    settings["cmake.configurePreset"] = "macos";
    settings["cmake.buildPreset"] = "macos";
    settings["clangd.arguments"] = Json::array({"--compile-commands-dir=${workspaceFolder}/build-macos"});
#elif defined(_WIN32)
    settings["cmake.buildDirectory"] = "${workspaceFolder}/build-windows";
    settings["cmake.configurePreset"] = "default";  // Uses CMakeUserPresets.json with ninja path
    settings["cmake.buildPreset"] = "default";
    settings["clangd.arguments"] = Json::array({"--compile-commands-dir=${workspaceFolder}/build-windows"});
#else
    settings["cmake.buildDirectory"] = "${workspaceFolder}/build-linux";
    settings["cmake.configurePreset"] = "linux";
    settings["cmake.buildPreset"] = "linux";
    settings["clangd.arguments"] = Json::array({"--compile-commands-dir=${workspaceFolder}/build-linux"});
#endif
    // Hide misleading launch button
    Json launchOptions;
    launchOptions["statusBarVisibility"] = "hidden";
    Json advancedOptions;
    advancedOptions["launch"] = launchOptions;
    settings["cmake.options.advanced"] = advancedOptions;
    // IntelliSense providers
    settings["C_Cpp.default.configurationProvider"] = "ms-vscode.cmake-tools";
    saveJson(settings, vscodePath + "/settings.json");

    // tasks.json
    Json tasks;
    tasks["version"] = "2.0.0";

    Json task;
    task["label"] = "CMake: build";
    task["type"] = "cmake";
    task["command"] = "build";
    task["problemMatcher"] = Json::array();

    Json group;
    group["kind"] = "build";
    group["isDefault"] = true;
    task["group"] = group;

    tasks["tasks"] = Json::array();
    tasks["tasks"].push_back(task);

    if (settings_.generateWebBuild) {
        Json webTask;
        webTask["label"] = "Build Web";
        webTask["type"] = "shell";
#ifdef __APPLE__
        webTask["command"] = "./build-web.command";
#elif defined(_WIN32)
        webTask["command"] = ".\\build-web.bat";
#else
        webTask["command"] = "./build-web.sh";
#endif
        webTask["problemMatcher"] = Json::array();
        webTask["group"] = "build";
        tasks["tasks"].push_back(webTask);
    }

    saveJson(tasks, vscodePath + "/tasks.json");

    // extensions.json
    Json extensions;
    extensions["recommendations"] = Json::array();
    extensions["recommendations"].push_back("ms-vscode.cmake-tools");

    if (settings_.ideType == IdeType::Cursor) {
        extensions["recommendations"].push_back("llvm-vs-code-extensions.vscode-clangd");
    } else {
        extensions["recommendations"].push_back("ms-vscode.cpptools");
    }
    extensions["recommendations"].push_back("vadimcn.vscode-lldb");

    saveJson(extensions, vscodePath + "/extensions.json");
}

void ProjectGenerator::generateXcodeProject(const string& path) {
    string xcodePath = path + "/xcode";
    if (fs::exists(xcodePath)) {
        fs::remove_all(xcodePath);
    }
    fs::create_directories(xcodePath);

    string cmd = "cd \"" + xcodePath + "\" && /opt/homebrew/bin/cmake -G Xcode ..";
    log("Running: cmake -G Xcode");

    auto [result, output] = executeCommand(cmd);
    if (!output.empty()) {
        log(output);
    }
    if (result != 0) {
        throw runtime_error("Failed to generate Xcode project");
    }

    // Generate Debug/Release schemes
    IdeHelper::generateXcodeSchemes(path);
}

void ProjectGenerator::generateVisualStudioProject(const string& path) {
    // Check CMake version for VS2026 (skip if using VS-bundled cmake)
    if (!settings_.installedVsVersions.empty() &&
        settings_.selectedVsIndex < (int)settings_.installedVsVersions.size()) {
        const auto& vsInfo = settings_.installedVsVersions[settings_.selectedVsIndex];
        // Only check PATH cmake version if not using VS-bundled cmake
        if (vsInfo.cmakePath.empty()) {
            string errorMsg;
            if (!VsDetector::checkCmakeVersionForVs(vsInfo.version, errorMsg)) {
                throw runtime_error(errorMsg);
            }
        }
    }

    string vsPath = path + "/vs";
    if (fs::exists(vsPath)) {
        fs::remove_all(vsPath);
    }
    fs::create_directories(vsPath);

    // Get generator name and cmake path from selected VS version
    string generator = "Visual Studio 17 2022";  // Default
    string cmakeBin = "cmake";  // Default to PATH
    if (!settings_.installedVsVersions.empty() &&
        settings_.selectedVsIndex < (int)settings_.installedVsVersions.size()) {
        generator = settings_.installedVsVersions[settings_.selectedVsIndex].generator;
        // Use VS-bundled cmake if available
        const string& vsCmake = settings_.installedVsVersions[settings_.selectedVsIndex].cmakePath;
        if (!vsCmake.empty()) {
            cmakeBin = "\"" + vsCmake + "\"";
        }
    }

#ifdef _WIN32
    string cmd = "cd /d \"" + vsPath + "\" && " + cmakeBin + " -G \"" + generator + "\" ..";
#else
    string cmd = "cd \"" + vsPath + "\" && cmake -G \"" + generator + "\" ..";
#endif
    log("Running: " + cmakeBin + " -G \"" + generator + "\"");

    auto [result, output] = executeCommand(cmd);
    if (!output.empty()) {
        log(output);
    }
    if (result != 0) {
        throw runtime_error("Failed to generate Visual Studio project");
    }
}

void ProjectGenerator::generateWebBuildFiles(const string& path) {
#ifdef _WIN32
    // Windows: build-web.bat
    string scriptPath = path + "/build-web.bat";
    ofstream file(scriptPath);
    file << "@echo off\n";
    file << "setlocal\n\n";
    file << "REM TrussC Web Build Script (Windows)\n\n";
    file << "if not exist emscripten mkdir emscripten\n";
    file << "cd emscripten\n\n";
    file << "call emcmake cmake ..\n";
    file << "cmake --build .\n\n";
    file << "echo.\n";
    file << "echo Build complete! Output files are in bin\\\n";
    file << "echo To test locally:\n";
    file << "echo   cd ..\\bin ^&^& python -m http.server 8080\n";
    file << "echo   Open http://localhost:8080/%~n0.html\n";
    file.close();
#elif defined(__APPLE__)
    // macOS: build-web.command
    string scriptPath = path + "/build-web.command";
    ofstream file(scriptPath);
    file << "#!/bin/bash\n";
    file << "# TrussC Web Build Script (macOS)\n\n";
    file << "cd \"$(dirname \"$0\")\"\n\n";
    file << "mkdir -p emscripten\n";
    file << "cd emscripten\n\n";
    file << "emcmake cmake ..\n";
    file << "cmake --build .\n\n";
    file << "echo \"\"\n";
    file << "echo \"Build complete! Output files are in bin/\"\n";
    file << "echo \"To test locally:\"\n";
    file << "echo \"  cd ../bin && python3 -m http.server 8080\"\n";
    file << "echo \"  Open http://localhost:8080/$(basename $(pwd)).html\"\n";
    file.close();
    chmod(scriptPath.c_str(), 0755);
#else
    // Linux: build-web.sh
    string scriptPath = path + "/build-web.sh";
    ofstream file(scriptPath);
    file << "#!/bin/bash\n";
    file << "# TrussC Web Build Script (Linux)\n\n";
    file << "cd \"$(dirname \"$0\")\"\n\n";
    file << "mkdir -p emscripten\n";
    file << "cd emscripten\n\n";
    file << "emcmake cmake ..\n";
    file << "cmake --build .\n\n";
    file << "echo \"\"\n";
    file << "echo \"Build complete! Output files are in bin/\"\n";
    file << "echo \"To test locally:\"\n";
    file << "echo \"  cd ../bin && python3 -m http.server 8080\"\n";
    file << "echo \"  Open http://localhost:8080/$(basename $(pwd)).html\"\n";
    file.close();
    chmod(scriptPath.c_str(), 0755);
#endif
}

// Run CMake configure to generate compile_commands.json for IntelliSense.
//
// On Windows, we use VS-bundled cmake and ninja via vcvarsall.bat.
// This sets up the environment (cl.exe, ninja, cmake in PATH) properly.
//
// Unix Makefiles (macOS/Linux) and Ninja both support CMAKE_EXPORT_COMPILE_COMMANDS.
// Visual Studio generator does NOT support compile_commands.json.
void ProjectGenerator::runCMakeConfigure(const string& path) {
#ifdef _WIN32
    string preset = "default";  // Uses CMakeUserPresets.json which has ninja path

    // Get vcvarsall.bat path from detected VS versions
    string vcvarsallPath;
    if (!settings_.installedVsVersions.empty() &&
        settings_.selectedVsIndex < (int)settings_.installedVsVersions.size()) {
        vcvarsallPath = settings_.installedVsVersions[settings_.selectedVsIndex].vcvarsallPath;
    }

    if (vcvarsallPath.empty()) {
        log("Visual Studio not found. CMake will auto-configure when you open the project.");
        return;
    }

    log("Running CMake configure (preset: " + preset + ")...");

    // Use vcvarsall.bat to set up VS environment, then run cmake
    // CMAKE_MAKE_PROGRAM is set in CMakeUserPresets.json
    string cmd = "cmd /c \"\"" + vcvarsallPath + "\" x64 && cd /d \"" + path + "\" && cmake --preset " + preset + "\"";

    auto [result, output] = executeCommand(cmd);

    if (!output.empty()) {
        log(output);
    }

    if (result != 0) {
        throw runtime_error("CMake configure failed");
    }

    log("CMake configure complete. compile_commands.json generated.");
#else
    // Determine preset name based on OS
#ifdef __APPLE__
    string preset = "macos";
#else
    string preset = "linux";
#endif

    log("Running CMake configure (preset: " + preset + ")...");

    string cmd = "cd \"" + path + "\" && cmake --preset " + preset;

    auto [result, output] = executeCommand(cmd);

    if (!output.empty()) {
        log(output);
    }

    if (result != 0) {
        throw runtime_error("CMake configure failed");
    }

    log("CMake configure complete. compile_commands.json generated.");
#endif
}
