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

// Get cmake path (handles macOS where PATH may not be set in GUI apps)
static string getCmakePath() {
#ifdef __APPLE__
    // Try common cmake locations on macOS
    const char* paths[] = {
        "/opt/homebrew/bin/cmake",  // Apple Silicon Homebrew
        "/usr/local/bin/cmake",     // Intel Homebrew
        "/Applications/CMake.app/Contents/bin/cmake",  // CMake.app
        "cmake"  // Fallback to PATH
    };
    for (const char* path : paths) {
        if (fs::exists(path)) {
            return path;
        }
    }
    return "cmake";  // Fallback
#else
    return "cmake";
#endif
}

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

// Get TRUSSC_DIR value for CMakePresets.json
//
// DESIGN NOTE - Why relative vs absolute path:
//
// 1. Examples inside TrussC repo (../../../trussc works):
//    - Return empty string -> use CMakeLists.txt fallback (relative path)
//    - Relative path is correct here because examples move WITH trussc
//    - If you move the entire TrussC repo, examples still work
//
// 2. User projects outside TrussC repo:
//    - Return ABSOLUTE path to trussc
//    - User projects should NOT use relative paths because:
//      a) trussc location is typically fixed (user won't move it)
//      b) User might move their project to a different location
//      c) Absolute path ensures the project works after being moved
//
// Summary: Examples move with trussc, user projects move independently.
//
string ProjectGenerator::getTrusscDirValue(const string& projectPath) {
    fs::path project = fs::absolute(projectPath);
    fs::path trussc = fs::absolute(settings_.tcRoot) / "trussc";

    // Check if template default path (../../../trussc) would work
    fs::path templateDefault = project / "../../../trussc";
    if (fs::exists(templateDefault) && fs::equivalent(templateDefault, trussc)) {
        // Template default works - don't override TRUSSC_DIR
        return "";
    }

    // Template default won't work - use absolute path
    string result = trussc.string();
    for (char& c : result) {
        if (c == '\\') c = '/';
    }
    return result;
}

// DESIGN NOTE: CMakeLists.txt is NOT modified by projectGenerator.
// All project-specific configuration (TRUSSC_DIR, paths, etc.) goes into CMakePresets.json.
// This keeps CMakeLists.txt as a simple copy from the template, making it easier to maintain
// and allowing manual project setup without projectGenerator.

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

void ProjectGenerator::writeCMakePresets(const string& destPath) {
    log("Writing CMakePresets.json...");

    // Get TRUSSC_DIR value for this project
    string trusscDir = getTrusscDirValue(destPath);

    Json presets;
    presets["version"] = 6;
    presets["configurePresets"] = Json::array();
    presets["buildPresets"] = Json::array();

#ifdef __APPLE__
    // macOS preset
    Json macosPreset;
    macosPreset["name"] = "macos";
    macosPreset["displayName"] = "macOS";
    macosPreset["binaryDir"] = "${sourceDir}/build-macos";
    macosPreset["generator"] = "Unix Makefiles";
    macosPreset["cacheVariables"]["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON";
    // Only set TRUSSC_DIR if template default won't work (see getTrusscDirValue)
    if (!trusscDir.empty()) {
        macosPreset["cacheVariables"]["TRUSSC_DIR"] = trusscDir;
    }
    presets["configurePresets"].push_back(macosPreset);

    Json macosBuildPreset;
    macosBuildPreset["name"] = "macos";
    macosBuildPreset["configurePreset"] = "macos";
    macosBuildPreset["jobs"] = 4;
    presets["buildPresets"].push_back(macosBuildPreset);

#elif defined(_WIN32)
    // Windows preset with ninja path and environment
    Json windowsPreset;
    windowsPreset["name"] = "windows";
    windowsPreset["displayName"] = "Windows";
    windowsPreset["binaryDir"] = "${sourceDir}/build-windows";
    windowsPreset["generator"] = "Ninja";
    windowsPreset["cacheVariables"]["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON";
    // Only set TRUSSC_DIR if template default won't work (see getTrusscDirValue)
    if (!trusscDir.empty()) {
        windowsPreset["cacheVariables"]["TRUSSC_DIR"] = trusscDir;
    }

    // Add ninja path and environment if VS info available
    if (!settings_.installedVsVersions.empty() &&
        settings_.selectedVsIndex < (int)settings_.installedVsVersions.size()) {
        const auto& vsInfo = settings_.installedVsVersions[settings_.selectedVsIndex];

        // Convert path to forward slashes for JSON
        auto toForwardSlash = [](string path) {
            for (char& c : path) {
                if (c == '\\') c = '/';
            }
            return path;
        };

        if (!vsInfo.ninjaPath.empty()) {
            windowsPreset["cacheVariables"]["CMAKE_MAKE_PROGRAM"] = toForwardSlash(vsInfo.ninjaPath);
        }

        // Add environment for INCLUDE, LIB, PATH
        if (!vsInfo.vcToolsVersion.empty() && !vsInfo.windowsSdkVersion.empty()) {
            string vsPath = toForwardSlash(vsInfo.installPath);
            string vcToolsVer = vsInfo.vcToolsVersion;
            string sdkVer = vsInfo.windowsSdkVersion;

            // INCLUDE paths
            string includePath =
                vsPath + "/VC/Tools/MSVC/" + vcToolsVer + "/include;" +
                "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/ucrt;" +
                "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/shared;" +
                "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/um;" +
                "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/winrt";

            // LIB paths
            string libPath =
                vsPath + "/VC/Tools/MSVC/" + vcToolsVer + "/lib/x64;" +
                "C:/Program Files (x86)/Windows Kits/10/Lib/" + sdkVer + "/ucrt/x64;" +
                "C:/Program Files (x86)/Windows Kits/10/Lib/" + sdkVer + "/um/x64";

            // PATH additions
            string pathAddition =
                vsPath + "/VC/Tools/MSVC/" + vcToolsVer + "/bin/Hostx64/x64;" +
                "C:/Program Files (x86)/Windows Kits/10/bin/" + sdkVer + "/x64;$penv{PATH}";

            windowsPreset["environment"]["INCLUDE"] = includePath;
            windowsPreset["environment"]["LIB"] = libPath;
            windowsPreset["environment"]["PATH"] = pathAddition;
        }
    }
    presets["configurePresets"].push_back(windowsPreset);

    Json windowsBuildPreset;
    windowsBuildPreset["name"] = "windows";
    windowsBuildPreset["configurePreset"] = "windows";
    presets["buildPresets"].push_back(windowsBuildPreset);

#else
    // Linux preset
    Json linuxPreset;
    linuxPreset["name"] = "linux";
    linuxPreset["displayName"] = "Linux";
    linuxPreset["binaryDir"] = "${sourceDir}/build-linux";
    linuxPreset["generator"] = "Unix Makefiles";
    linuxPreset["cacheVariables"]["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON";
    // Only set TRUSSC_DIR if template default won't work (see getTrusscDirValue)
    if (!trusscDir.empty()) {
        linuxPreset["cacheVariables"]["TRUSSC_DIR"] = trusscDir;
    }
    presets["configurePresets"].push_back(linuxPreset);

    Json linuxBuildPreset;
    linuxBuildPreset["name"] = "linux";
    linuxBuildPreset["configurePreset"] = "linux";
    linuxBuildPreset["jobs"] = 4;
    presets["buildPresets"].push_back(linuxBuildPreset);
#endif

    // Add web preset if web build is enabled
    if (settings_.generateWebBuild) {
        Json webPreset;
        webPreset["name"] = "web";
        webPreset["displayName"] = "Web (Emscripten)";
        webPreset["binaryDir"] = "${sourceDir}/build-web";
        webPreset["generator"] = "Unix Makefiles";
        webPreset["toolchainFile"] = "$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake";
        webPreset["cacheVariables"]["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON";
        // Only set TRUSSC_DIR if template default won't work (see getTrusscDirValue)
        if (!trusscDir.empty()) {
            webPreset["cacheVariables"]["TRUSSC_DIR"] = trusscDir;
        }
        presets["configurePresets"].push_back(webPreset);

        Json webBuildPreset;
        webBuildPreset["name"] = "web";
        webBuildPreset["configurePreset"] = "web";
        webBuildPreset["jobs"] = 4;
        presets["buildPresets"].push_back(webBuildPreset);
    }

    saveJson(presets, destPath + "/CMakePresets.json");
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
                // Skip build directories, output, and generated files
                if (name == "build" || name == "bin" ||
                    name == "build-macos" || name == "build-windows" || name == "build-linux" ||
                    name == "CMakePresets.json" || name == "CMakeUserPresets.json") {
                    continue;
                }
                fs::copy(entry.path(), destPath + "/" + name,
                         fs::copy_options::recursive);
            }
        }

        log("Writing addons.make...");
        writeAddonsMake(destPath);

        // Write CMakePresets.json (OS-specific with TRUSSC_DIR)
        // CMakeLists.txt is already copied from template (no modification needed)
        writeCMakePresets(destPath);

        // Generate IDE-specific files
        if (settings_.ideType == IdeType::VSCode || settings_.ideType == IdeType::Cursor) {
            log("Generating VSCode files...");
            generateVSCodeFiles(destPath);
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

        // Update addons.make
        log("Updating addons.make...");
        writeAddonsMake(projectPath);

        // Write CMakePresets.json (OS-specific with TRUSSC_DIR)
        // CMakeLists.txt is NOT modified - it's a simple copy from template
        writeCMakePresets(projectPath);

        // Regenerate IDE-specific files
        if (settings_.ideType == IdeType::VSCode || settings_.ideType == IdeType::Cursor) {
            log("Updating VSCode files...");
            generateVSCodeFiles(projectPath);
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
    settings["cmake.configurePreset"] = "windows";
    settings["cmake.buildPreset"] = "windows";
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

    string cmd = "cd \"" + xcodePath + "\" && " + getCmakePath() + " -G Xcode ..";
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
    string cmd = "cd \"" + vsPath + "\" && " + getCmakePath() + " -G \"" + generator + "\" ..";
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
    string preset = "windows";

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

    string cmd = "cd \"" + path + "\" && " + getCmakePath() + " --preset " + preset;

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
