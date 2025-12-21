// =============================================================================
// tcApp.cpp - TrussC Project Generator
// =============================================================================

#include "tcApp.h"
#include <filesystem>
#include <fstream>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>  // chmod
#endif

namespace fs = std::filesystem;

void tcApp::setup() {
    imguiSetup();

    // Power-saving mode: update at 30fps, draw is event-driven
    setUpdateFps(30);
    setDrawFps(0);  // Disable automatic drawing

    // Config file path (~/.trussc/config.json)
    string home = getenv("HOME") ? getenv("HOME") : "";
    configPath = home + "/.trussc/config.json";

    // Load config
    loadConfig();

    // Show dialog if TC_ROOT is not set
    if (tcRoot.empty()) {
        showSetupDialog = true;
    } else {
        strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);
        scanAddons();
    }

    // Default save location
    if (projectDir.empty()) {
        projectDir = home + "/Projects";
    }
    strncpy(projectDirBuf, projectDir.c_str(), sizeof(projectDirBuf) - 1);

    // Auto-switch to Update mode if previous project folder exists
    if (!projectDir.empty() && !projectName.empty()) {
        string lastProjectPath = projectDir + "/" + projectName;
        if (fs::is_directory(lastProjectPath)) {
            importProject(lastProjectPath);
        }
    }

    // Initial draw
    redraw();
}

void tcApp::update() {
    // Redraw every frame while generating (for pulsing animation)
    if (isGenerating) {
        redraw();
    }
}

// Redraw on mouse events
void tcApp::mousePressed(int x, int y, int button) {
    redraw();
}

void tcApp::mouseReleased(int x, int y, int button) {
    redraw();
}

void tcApp::mouseMoved(int x, int y) {
    // Redraw only when inside the window
    if (x >= 0 && x < getWindowWidth() && y >= 0 && y < getWindowHeight()) {
        redraw();
    }
}

void tcApp::mouseDragged(int x, int y, int button) {
    redraw();
}

// Redraw on key events
void tcApp::keyPressed(int key) {
    redraw();
}

void tcApp::keyReleased(int key) {
    redraw();
}

void tcApp::filesDropped(const vector<string>& files) {
    if (files.empty()) return;

    // Only process the first file/folder
    const string& path = files[0];

    // Check if it's a directory and import as project
    if (fs::is_directory(path)) {
        importProject(path);
    }

    // Draw twice: UI changes may not reflect until the next frame
    redraw(2);
}

void tcApp::draw() {
    clear(0.18f, 0.18f, 0.19f);

    imguiBegin();

    // Handle deferred import (to avoid crash during InputText edit)
    if (!pendingImportPath.empty()) {
        string pathToImport = pendingImportPath;
        pendingImportPath.clear();
        importProject(pathToImport);
    }

    // TC_ROOT setup dialog
    if (showSetupDialog) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(getWindowWidth(), getWindowHeight()));
        ImGui::Begin("Setup TC_ROOT", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        ImGui::Spacing();
        ImGui::Text("Setup TrussC");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("Please select the TrussC folder (e.g. TrussC).");
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::Text("TrussC Folder");
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##tcRoot", tcRootBuf, sizeof(tcRootBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            auto result = loadDialog("Select TrussC folder", true);
            if (result.success) {
                strncpy(tcRootBuf, result.filePath.c_str(), sizeof(tcRootBuf) - 1);
            }
            // Draw twice: UI changes may not reflect until the next frame
            redraw(2);
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (ImGui::Button("OK", ImVec2(120, 30))) {
            tcRoot = tcRootBuf;
            // Verify tc_vX.Y.Z folder (check if CMakeLists.txt exists)
            if (!tcRoot.empty() && fs::exists(tcRoot + "/trussc") && fs::exists(tcRoot + "/trussc/CMakeLists.txt")) {
                showSetupDialog = false;
                saveConfig();
                scanAddons();
            } else {
                setStatus("Invalid TrussC folder (CMakeLists.txt not found)", true);
            }
        }

        // Status message
        if (!statusMessage.empty() && statusIsError) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", statusMessage.c_str());
        }

        ImGui::End();
        imguiEnd();
        return;  // Don't draw main window
    }

    // Main window (fixed to full window size)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(getWindowWidth(), getWindowHeight()));
    ImGui::Begin("TrussC Project Generator", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    // Project name
    ImGui::Text("Project Name");
    ImGui::SetNextItemWidth(-80);
    if (isImportedProject) {
        ImGui::BeginDisabled();
    }
    ImGui::InputText("##projectName", projectNameBuf, sizeof(projectNameBuf));
    // Auto-switch to Update mode when input is deactivated (Enter or focus lost)
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!isImportedProject && strlen(projectNameBuf) > 0 && strlen(projectDirBuf) > 0) {
            string checkPath = string(projectDirBuf) + "/" + string(projectNameBuf);
            if (fs::is_directory(checkPath)) {
                pendingImportPath = checkPath;
            }
        }
    }
    if (isImportedProject) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        auto result = loadDialog("Select existing project", true);
        if (result.success) {
            importProject(result.filePath);
        }
        // Draw twice: UI changes may not reflect until the next frame
        redraw(2);
    }

    ImGui::Spacing();

    // Save location
    ImGui::Text("Location");
    ImGui::SetNextItemWidth(-80);
    if (isImportedProject) {
        ImGui::BeginDisabled();
    }
    ImGui::InputText("##projectDir", projectDirBuf, sizeof(projectDirBuf));
    // Auto-switch to Update mode when input is deactivated (Enter or focus lost)
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!isImportedProject && strlen(projectNameBuf) > 0 && strlen(projectDirBuf) > 0) {
            string checkPath = string(projectDirBuf) + "/" + string(projectNameBuf);
            if (fs::is_directory(checkPath)) {
                pendingImportPath = checkPath;
            }
        }
    }
    if (isImportedProject) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (isImportedProject) {
        if (ImGui::Button("New")) {
            resetToNewProject();
        }
    } else {
        if (ImGui::Button("Browse##dir")) {
            auto result = loadDialog("Select project location", true);
            if (result.success) {
                strncpy(projectDirBuf, result.filePath.c_str(), sizeof(projectDirBuf) - 1);
                projectDir = projectDirBuf;
                saveConfig();
                // Auto-switch to Update mode if project folder exists (deferred to next frame)
                if (strlen(projectNameBuf) > 0) {
                    string checkPath = string(projectDirBuf) + "/" + string(projectNameBuf);
                    if (fs::is_directory(checkPath)) {
                        pendingImportPath = checkPath;
                    }
                }
            }
            // Draw twice: UI changes may not reflect until the next frame
            redraw(2);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Addon selection
    ImGui::Text("Addons");
    ImGui::BeginChild("##addons", ImVec2(0, 100), true);
    if (addons.empty()) {
        ImGui::TextDisabled("No addons available");
    } else {
        for (size_t i = 0; i < addons.size(); i++) {
            bool selected = addonSelected[i] != 0;
            if (ImGui::Checkbox(addons[i].c_str(), &selected)) {
                addonSelected[i] = selected ? 1 : 0;
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // IDE selection
    ImGui::Text("IDE");
    ImGui::SetNextItemWidth(-1);
    const char* ideItems[] = { "CMake only", "VSCode", "Cursor", "Xcode (macOS)", "Visual Studio (Windows)" };
    int ideIndex = static_cast<int>(ideType);
    if (ImGui::Combo("##ide", &ideIndex, ideItems, 5)) {
        ideType = static_cast<IdeType>(ideIndex);
        saveConfig();  // Save on IDE change
    }

    ImGui::Spacing();

    // Web build option
    if (ImGui::Checkbox("Web (Emscripten)", &generateWebBuild)) {
        saveConfig();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Generate build scripts for WebAssembly.\nRequires Emscripten SDK installed.\nClick to open download page.");
    }
    if (ImGui::IsItemClicked()) {
#ifdef __APPLE__
        system("open https://emscripten.org/docs/getting_started/downloads.html");
#elif defined(_WIN32)
        system("start https://emscripten.org/docs/getting_started/downloads.html");
#else
        system("xdg-open https://emscripten.org/docs/getting_started/downloads.html");
#endif
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Generate/Update button
    if (isGenerating) {
        // Generating: pulsing button
        float t = getElapsedTime();
        float pulse = 0.5f + 0.3f * sinf(t * 4.0f);  // Pulsing
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, pulse));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.8f, pulse));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.8f, pulse));
        ImGui::Button("Generating...", ImVec2(-1, 40));
        ImGui::PopStyleColor(3);
    } else if (isImportedProject) {
        // Arrange Update and Open in IDE side by side
        float buttonWidth = (ImGui::GetContentRegionAvail().x - 8) / 2;
        if (ImGui::Button("Update Project", ImVec2(buttonWidth, 40))) {
            startUpdate();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open in IDE", ImVec2(buttonWidth, 40))) {
            openInIde(importedProjectPath);
        }
    } else {
        if (ImGui::Button("Generate Project", ImVec2(-1, 40))) {
            projectName = projectNameBuf;
            projectDir = projectDirBuf;
            startGenerate();
        }
    }

    // Generation log display
    if (isGenerating || !generatingLog.empty()) {
        ImGui::Spacing();
        ImGui::BeginChild("##log", ImVec2(0, 85), true);
        lock_guard<mutex> lock(logMutex);
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 10);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", generatingLog.c_str());
        ImGui::PopTextWrapPos();
        // Auto-scroll to bottom
        if (isGenerating) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    // Status message (wrap at window width - 10px)
    if (!statusMessage.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 10);
        if (statusIsError) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", statusMessage.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "%s", statusMessage.c_str());
        }
        ImGui::PopTextWrapPos();
    }

    // TC_ROOT settings button (bottom)
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 35);
    ImGui::Separator();
    if (ImGui::SmallButton("Settings...")) {
        showSetupDialog = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("TrussC: %s", tcRoot.c_str());

    ImGui::End();

    imguiEnd();
}

void tcApp::cleanup() {
    // Save current state on exit
    projectName = projectNameBuf;
    projectDir = projectDirBuf;
    tcLogNotice("tcApp") << "cleanup: saving projectName=" << projectName << ", projectDir=" << projectDir;
    saveConfig();

    imguiShutdown();
}

void tcApp::loadConfig() {
    tcLogNotice("tcApp") << "loadConfig: configPath = " << configPath;
    if (!fs::exists(configPath)) {
        tcLogNotice("tcApp") << "loadConfig: config file not found";
        return;
    }

    Json config = loadJson(configPath);
    if (config.empty()) {
        tcLogNotice("tcApp") << "loadConfig: config is empty";
        return;
    }

    if (config.contains("tc_root")) {
        tcRoot = config["tc_root"].get<string>();
    }
    if (config.contains("last_project_dir")) {
        projectDir = config["last_project_dir"].get<string>();
    }
    if (config.contains("last_project_name")) {
        projectName = config["last_project_name"].get<string>();
        strncpy(projectNameBuf, projectName.c_str(), sizeof(projectNameBuf) - 1);
    }
    if (config.contains("ide_type")) {
        ideType = static_cast<IdeType>(config["ide_type"].get<int>());
    }
    if (config.contains("generate_web_build")) {
        generateWebBuild = config["generate_web_build"].get<bool>();
    }
    tcLogNotice("tcApp") << "loadConfig: projectDir = " << projectDir << ", projectName = " << projectName;
}

void tcApp::saveConfig() {
    // Create ~/.trussc/ directory
    fs::path configDir = fs::path(configPath).parent_path();
    if (!fs::exists(configDir)) {
        fs::create_directories(configDir);
    }

    Json config;
    config["tc_root"] = tcRoot;
    config["last_project_dir"] = projectDir;
    config["last_project_name"] = projectName;
    config["ide_type"] = static_cast<int>(ideType);
    config["generate_web_build"] = generateWebBuild;
    saveJson(config, configPath);
}

void tcApp::scanAddons() {
    addons.clear();
    addonSelected.clear();

    if (tcRoot.empty()) return;

    string addonsPath = tcRoot + "/addons";
    if (!fs::exists(addonsPath)) return;

    // Enumerate addon folders
    for (const auto& entry : fs::directory_iterator(addonsPath)) {
        if (entry.is_directory()) {
            string name = entry.path().filename().string();
            if (name.substr(0, 3) == "tcx") {
                addons.push_back(name);
                addonSelected.push_back(0);
            }
        }
    }

    // Sort
    sort(addons.begin(), addons.end());
}

string tcApp::getTemplatePath() {
    if (tcRoot.empty()) return "";
    return tcRoot + "/examples/templates/emptyExample";
}

// Calculate TRUSSC_DIR value for CMakeLists.txt
// Always uses relative path from project to trussc folder
string tcApp::getTrusscDirValue(const string& projectPath) {
    fs::path projPath = fs::weakly_canonical(projectPath);
    fs::path trusscPath = fs::weakly_canonical(tcRoot + "/trussc");

    // Calculate relative path from project to trussc
    fs::path relPath = fs::relative(trusscPath, projPath);
    return "${CMAKE_CURRENT_SOURCE_DIR}/" + relPath.string();
}

bool tcApp::generateProject() {
    // Validation
    if (projectName.empty()) {
        setStatus("Project name is required", true);
        return false;
    }

    if (projectDir.empty()) {
        setStatus("Location is required", true);
        return false;
    }

    if (tcRoot.empty()) {
        setStatus("TrussC folder not set", true);
        return false;
    }

    string templatePath = getTemplatePath();
    if (!fs::exists(templatePath)) {
        setStatus("Template not found", true);
        return false;
    }

    // Project path (remove trailing slashes)
    while (!projectDir.empty() && projectDir.back() == '/') {
        projectDir.pop_back();
    }
    string destPath = projectDir + "/" + projectName;
    bool folderExists = fs::is_directory(destPath);

    try {
        // Create parent directory
        fs::create_directories(projectDir);

        // Copy template only if folder doesn't exist (new project)
        if (!folderExists) {
            fs::create_directories(destPath);
            for (const auto& entry : fs::directory_iterator(templatePath)) {
                string name = entry.path().filename().string();
                if (name == "build" || name == "bin") {
                    continue;  // Skip
                }
                fs::copy(entry.path(), destPath / entry.path().filename(),
                         fs::copy_options::recursive);
            }
        }
        // If folder exists, just update CMakeLists.txt (like update mode)

        // Read CMakeLists.txt from template and write to project
        string templateCmakePath = templatePath + "/CMakeLists.txt";
        ifstream inFile(templateCmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();

        string content = buffer.str();

        // Replace TRUSSC_DIR (relative if inside tcRoot, absolute otherwise)
        // Template: set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
        // Result:  set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../trussc") or absolute path
        size_t pos = content.find("set(TRUSSC_DIR \"");
        if (pos != string::npos) {
            size_t endPos = content.find("\")", pos);
            if (endPos != string::npos) {
                content.replace(pos, endPos - pos + 2,
                    "set(TRUSSC_DIR \"" + getTrusscDirValue(destPath) + "\")");
            }
        }

        // Write to project
        string cmakePath = destPath + "/CMakeLists.txt";
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        // Generate addons.make
        string addonsMakePath = destPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // Generate IDE-specific files
        if (ideType == IdeType::VSCode || ideType == IdeType::Cursor) {
            generateVSCodeFiles(destPath);
        } else if (ideType == IdeType::Xcode) {
            generateXcodeProject(destPath);
        } else if (ideType == IdeType::VisualStudio) {
            generateVisualStudioProject(destPath);
        }

        saveConfig();
        return true;

    } catch (const exception& e) {
        setStatus(string("Error: ") + e.what(), true);
        return false;
    }
}

void tcApp::generateVSCodeFiles(const string& path) {
    // Create .vscode folder
    string vscodePath = path + "/.vscode";
    fs::create_directories(vscodePath);

    // launch.json (per-OS configuration)
    Json launch;
    launch["version"] = "0.2.0";
    launch["configurations"] = Json::array();

    Json config;
    config["name"] = "Debug";
    config["type"] = "lldb";
    config["request"] = "launch";
    config["cwd"] = "${workspaceFolder}";
    config["preLaunchTask"] = "CMake: build";

    // Per-OS program paths
    Json osx;
    osx["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.app/Contents/MacOS/${workspaceFolderBasename}";
    config["osx"] = osx;

    Json linux_cfg;
    linux_cfg["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}";
    config["linux"] = linux_cfg;

    Json windows;
    windows["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.exe";
    windows["type"] = "cppvsdbg";  // Use cppvsdbg on Windows
    config["windows"] = windows;

    launch["configurations"].push_back(config);
    saveJson(launch, vscodePath + "/launch.json");

    // settings.json
    Json settings;
    settings["cmake.buildDirectory"] = "${workspaceFolder}/build";
    settings["cmake.sourceDirectory"] = "${workspaceFolder}";
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
    saveJson(tasks, vscodePath + "/tasks.json");

    // extensions.json (recommended extensions based on IDE and OS)
    Json extensions;
    extensions["recommendations"] = Json::array();

    // CMake Tools - required for all
    extensions["recommendations"].push_back("ms-vscode.cmake-tools");

    // C/C++ IntelliSense - different for VSCode vs Cursor
    if (ideType == IdeType::Cursor) {
        // Cursor: use clangd (ms-vscode.cpptools is blocked)
        extensions["recommendations"].push_back("llvm-vs-code-extensions.vscode-clangd");
    } else {
        // VSCode: use Microsoft C/C++
        extensions["recommendations"].push_back("ms-vscode.cpptools");
    }

    // Debugger - CodeLLDB for macOS/Linux (Windows uses cppvsdbg, but include anyway)
    extensions["recommendations"].push_back("vadimcn.vscode-lldb");

    saveJson(extensions, vscodePath + "/extensions.json");
}

void tcApp::generateXcodeProject(const string& path) {
    // Delete build folder and run cmake -G Xcode
    // (Can't switch to Xcode generator if existing CMakeCache exists)
    string buildPath = path + "/build";
    if (fs::exists(buildPath)) {
        fs::remove_all(buildPath);
    }
    fs::create_directories(buildPath);

    // Use full path for cmake since PATH may not be set when running from GUI app
    // TRUSSC_DIR is written directly in CMakeLists.txt, no environment variable needed
    string cmd = "cd \"" + buildPath + "\" && /opt/homebrew/bin/cmake -G Xcode ..";
    tcLogNotice("tcApp") << "Xcode cmd: " << cmd;
    int result = system(cmd.c_str());

    if (result != 0) {
        tcLogWarning("tcApp") << "Failed to generate Xcode project (exit code: " << result << ")";
        return;
    }

    // Generate two schemes: Debug and Release
    generateXcodeSchemes(path);
}

void tcApp::generateXcodeSchemes(const string& path) {
    tcLogNotice("tcApp") << "generateXcodeSchemes called with path: " << path;

    string buildPath = path + "/build";
    string projectName = fs::path(path).filename().string();
    tcLogNotice("tcApp") << "buildPath: " << buildPath << ", projectName: " << projectName;

    // Find .xcodeproj
    string xcodeprojPath;
    for (const auto& entry : fs::directory_iterator(buildPath)) {
        if (entry.path().extension() == ".xcodeproj") {
            xcodeprojPath = entry.path().string();
            break;
        }
    }
    if (xcodeprojPath.empty()) {
        tcLogWarning("tcApp") << "No .xcodeproj found in " << buildPath;
        return;
    }
    tcLogNotice("tcApp") << "Found xcodeproj: " << xcodeprojPath;

    // Scheme directory
    string schemesDir = xcodeprojPath + "/xcshareddata/xcschemes";
    if (!fs::exists(schemesDir)) return;

    // Find original scheme file
    string originalScheme;
    for (const auto& entry : fs::directory_iterator(schemesDir)) {
        if (entry.path().extension() == ".xcscheme") {
            originalScheme = entry.path().string();
            break;
        }
    }
    if (originalScheme.empty()) return;

    // Read scheme contents
    ifstream schemeFile(originalScheme);
    if (!schemeFile.is_open()) return;
    stringstream buffer;
    buffer << schemeFile.rdbuf();
    string schemeContent = buffer.str();
    schemeFile.close();

    // Delete original scheme
    fs::remove(originalScheme);

    // Generate Debug scheme (keep default buildConfiguration = "Debug")
    string debugScheme = schemesDir + "/" + projectName + " Debug.xcscheme";
    ofstream debugFile(debugScheme);
    debugFile << schemeContent;
    debugFile.close();

    // Generate Release scheme (change buildConfiguration to RelWithDebInfo)
    string releaseContent = schemeContent;
    // Change all buildConfiguration="Debug" to RelWithDebInfo
    string searchStr = "buildConfiguration=\"Debug\"";
    string replaceStr = "buildConfiguration=\"RelWithDebInfo\"";
    size_t pos = 0;
    while ((pos = releaseContent.find(searchStr, pos)) != string::npos) {
        releaseContent.replace(pos, searchStr.length(), replaceStr);
        pos += replaceStr.length();
    }
    string releaseScheme = schemesDir + "/" + projectName + " Release.xcscheme";
    ofstream releaseFile(releaseScheme);
    releaseFile << releaseContent;
    releaseFile.close();

    tcLogNotice("tcApp") << "Generated Xcode schemes: Debug, Release";
}

void tcApp::generateVisualStudioProject(const string& path) {
    // Delete build folder and run cmake -G "Visual Studio 17 2022"
    string buildPath = path + "/build";
    if (fs::exists(buildPath)) {
        fs::remove_all(buildPath);
    }
    fs::create_directories(buildPath);

#ifdef _WIN32
    string cmd = "cd /d \"" + buildPath + "\" && cmake -G \"Visual Studio 17 2022\" ..";
#else
    // Generation only from macOS/Linux (open on Windows)
    string cmd = "cd \"" + buildPath + "\" && cmake -G \"Visual Studio 17 2022\" ..";
#endif
    tcLogNotice("tcApp") << "Visual Studio cmd: " << cmd;
    int result = system(cmd.c_str());

    if (result != 0) {
        tcLogWarning("tcApp") << "Failed to generate Visual Studio project (exit code: " << result << ")";
    }
}

void tcApp::openInIde(const string& path) {
    string cmd;

#ifdef __APPLE__
    switch (ideType) {
        case IdeType::VSCode:
            cmd = "open -a \"Visual Studio Code\" \"" + path + "\"";
            break;
        case IdeType::Cursor:
            cmd = "open -a \"Cursor\" \"" + path + "\"";
            break;
        case IdeType::Xcode: {
            // Find and open build/*.xcodeproj
            string buildPath = path + "/build";
            if (fs::exists(buildPath)) {
                for (const auto& entry : fs::directory_iterator(buildPath)) {
                    string name = entry.path().filename().string();
                    if (name.find(".xcodeproj") != string::npos) {
                        cmd = "open \"" + entry.path().string() + "\"";
                        break;
                    }
                }
            }
            if (cmd.empty()) {
                setStatus("Xcode project not found. Run Update first.", true);
                return;
            }
            break;
        }
        case IdeType::VisualStudio:
            setStatus("Visual Studio is not available on macOS", true);
            return;
        case IdeType::CMakeOnly:
            // Open in Terminal
            cmd = "open -a Terminal \"" + path + "\"";
            break;
    }
#else
    // Windows / Linux
    switch (ideType) {
        case IdeType::VSCode:
            cmd = "code \"" + path + "\"";
            break;
        case IdeType::Cursor:
            cmd = "cursor \"" + path + "\"";
            break;
        case IdeType::Xcode:
            setStatus("Xcode is not available on Windows/Linux", true);
            return;
        case IdeType::VisualStudio: {
            // Find and open build/*.sln
            string buildPath = path + "/build";
            if (fs::exists(buildPath)) {
                for (const auto& entry : fs::directory_iterator(buildPath)) {
                    string name = entry.path().filename().string();
                    if (name.find(".sln") != string::npos) {
#ifdef _WIN32
                        cmd = "start \"\" \"" + entry.path().string() + "\"";
#else
                        setStatus("Visual Studio is not available on Linux", true);
                        return;
#endif
                        break;
                    }
                }
            }
            if (cmd.empty()) {
                setStatus("Visual Studio project not found. Run Update first.", true);
                return;
            }
            break;
        }
        case IdeType::CMakeOnly:
#ifdef _WIN32
            cmd = "start cmd /k \"cd /d " + path + "\"";
#else
            cmd = "x-terminal-emulator --working-directory=\"" + path + "\" || gnome-terminal --working-directory=\"" + path + "\"";
#endif
            break;
    }
#endif

    if (!cmd.empty()) {
        tcLogNotice("tcApp") << "Open in IDE: " << cmd;
        system(cmd.c_str());
    }
}

void tcApp::setStatus(const string& msg, bool isError) {
    statusMessage = msg;
    statusIsError = isError;
}

void tcApp::importProject(const string& path) {
    // Get project name (folder name)
    projectName = fs::path(path).filename().string();
    strncpy(projectNameBuf, projectName.c_str(), sizeof(projectNameBuf) - 1);

    // Set save location
    projectDir = fs::path(path).parent_path().string();
    strncpy(projectDirBuf, projectDir.c_str(), sizeof(projectDirBuf) - 1);

    // Try to parse TRUSSC_DIR from existing CMakeLists.txt (if it exists)
    string cmakePath = path + "/CMakeLists.txt";
    if (fs::exists(cmakePath)) {
        ifstream inFile(cmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();
        string content = buffer.str();

        // Parse TRUSSC_DIR to extract TC_ROOT
        // Format: set(TRUSSC_DIR "/path/to/tc_root/trussc") or
        //         set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../trussc")
        size_t pos = content.find("set(TRUSSC_DIR \"");
        if (pos != string::npos) {
            size_t start = pos + 16;  // length of 'set(TRUSSC_DIR "'
            size_t end = content.find("\"", start);
            if (end != string::npos) {
                string trusscDir = content.substr(start, end - start);
                string importedTcRoot;

                // Check if it's a relative path (starts with ${CMAKE_CURRENT_SOURCE_DIR}/)
                const string cmakePrefix = "${CMAKE_CURRENT_SOURCE_DIR}/";
                if (trusscDir.find(cmakePrefix) == 0) {
                    // Relative path: resolve against project path
                    string relativePath = trusscDir.substr(cmakePrefix.length());
                    fs::path trusscPath = fs::weakly_canonical(fs::path(path) / relativePath);
                    importedTcRoot = trusscPath.parent_path().string();
                } else {
                    // Absolute path: remove /trussc suffix to get TC_ROOT
                    importedTcRoot = trusscDir;
                    if (trusscDir.size() > 7 && trusscDir.substr(trusscDir.size() - 7) == "/trussc") {
                        importedTcRoot = trusscDir.substr(0, trusscDir.size() - 7);
                    }
                }

                // Update tcRoot if valid path (check trussc/CMakeLists.txt)
                if (!importedTcRoot.empty() && fs::exists(importedTcRoot + "/trussc/CMakeLists.txt")) {
                    tcRoot = importedTcRoot;
                    strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);
                    saveConfig();
                    scanAddons();
                }
            }
        }
    }

    // Load addons from addons.make
    for (size_t i = 0; i < addons.size(); i++) {
        addonSelected[i] = 0;
    }

    string addonsMakePath = path + "/addons.make";
    if (fs::exists(addonsMakePath)) {
        ifstream addonsFile(addonsMakePath);
        string line;
        while (getline(addonsFile, line)) {
            // Remove whitespace
            size_t start = line.find_first_not_of(" \t");
            if (start == string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            string addonName = line.substr(start, end - start + 1);

            // Skip comment lines
            if (addonName.empty() || addonName[0] == '#') continue;

            // Find and select from addon list
            for (size_t i = 0; i < addons.size(); i++) {
                if (addons[i] == addonName) {
                    addonSelected[i] = 1;
                    break;
                }
            }
        }
        addonsFile.close();
    }

    // Set import state
    isImportedProject = true;
    importedProjectPath = path;
    setStatus("Project imported: " + projectName);
}

bool tcApp::updateProject() {
    if (!isImportedProject || importedProjectPath.empty()) {
        setStatus("No project imported", true);
        return false;
    }

    string cmakePath = importedProjectPath + "/CMakeLists.txt";

    try {
        // Read new CMakeLists.txt from template (always overwrite)
        string templatePath = getTemplatePath();
        string templateCmakePath = templatePath + "/CMakeLists.txt";

        ifstream inFile(templateCmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();
        string content = buffer.str();

        // Replace TRUSSC_DIR (relative if inside tcRoot, absolute otherwise)
        size_t pos = content.find("set(TRUSSC_DIR \"");
        if (pos != string::npos) {
            size_t endPos = content.find("\")", pos);
            if (endPos != string::npos) {
                content.replace(pos, endPos - pos + 2,
                    "set(TRUSSC_DIR \"" + getTrusscDirValue(importedProjectPath) + "\")");
            }
        }

        // Write back
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        // Update addons.make
        string addonsMakePath = importedProjectPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // Copy .gitignore if it doesn't exist
        string gitignorePath = importedProjectPath + "/.gitignore";
        if (!fs::exists(gitignorePath)) {
            string templateGitignore = getTemplatePath() + "/.gitignore";
            if (fs::exists(templateGitignore)) {
                fs::copy_file(templateGitignore, gitignorePath);
            }
        }

        // Generate IDE-specific files
        if (ideType == IdeType::VSCode || ideType == IdeType::Cursor) {
            generateVSCodeFiles(importedProjectPath);
        } else if (ideType == IdeType::Xcode) {
            generateXcodeProject(importedProjectPath);
        } else if (ideType == IdeType::VisualStudio) {
            generateVisualStudioProject(importedProjectPath);
        }

        return true;

    } catch (const exception& e) {
        setStatus(string("Error: ") + e.what(), true);
        return false;
    }
}

void tcApp::resetToNewProject() {
    isImportedProject = false;
    importedProjectPath = "";
    // Keep previous projectName value (don't reset)

    // Reset addon selection
    for (size_t i = 0; i < addonSelected.size(); i++) {
        addonSelected[i] = 0;
    }

    setStatus("");
}

void tcApp::startGenerate() {
    if (isGenerating) return;

    // Remove trailing slashes (if 2+ chars and ends with /)
    while (projectDir.size() > 1 && projectDir.back() == '/') {
        projectDir.pop_back();
    }
    strncpy(projectDirBuf, projectDir.c_str(), sizeof(projectDirBuf) - 1);

    isGenerating = true;
    setStatus("");  // Clear previous status
    {
        lock_guard<mutex> lock(logMutex);
        generatingLog = "Starting project generation...";
    }

    // Run in thread
    thread([this]() {
        doGenerateProject();
        isGenerating = false;
    }).detach();
}

void tcApp::startUpdate() {
    if (isGenerating) return;

    // Remove trailing slashes (if 2+ chars and ends with /)
    while (tcRoot.size() > 1 && tcRoot.back() == '/') {
        tcRoot.pop_back();
    }
    strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);

    isGenerating = true;
    setStatus("");  // Clear previous status
    {
        lock_guard<mutex> lock(logMutex);
        generatingLog = "Starting project update...";
    }

    // Run in thread
    thread([this]() {
        doUpdateProject();
        isGenerating = false;
    }).detach();
}

void tcApp::doGenerateProject() {
    auto log = [this](const string& msg) {
        lock_guard<mutex> lock(logMutex);
        generatingLog += msg + "\n";
        redraw();
    };

    // Validation
    if (projectName.empty()) {
        log("Error: Project name is required");
        setStatus("Project name is required", true);
        return;
    }

    if (projectDir.empty()) {
        log("Error: Location is required");
        setStatus("Location is required", true);
        return;
    }

    if (tcRoot.empty()) {
        log("Error: TrussC folder not set");
        setStatus("TrussC folder not set", true);
        return;
    }

    string templatePath = getTemplatePath();
    if (!fs::exists(templatePath)) {
        log("Error: Template not found");
        setStatus("Template not found", true);
        return;
    }

    // Project path (remove trailing slashes)
    string projDir = projectDir;
    while (!projDir.empty() && projDir.back() == '/') {
        projDir.pop_back();
    }
    string destPath = projDir + "/" + projectName;
    bool folderExists = fs::is_directory(destPath);

    try {
        // Create parent directory
        fs::create_directories(projDir);

        // Copy template only if folder doesn't exist (new project)
        if (!folderExists) {
            log("Creating project directory...");
            fs::create_directories(destPath);
            for (const auto& entry : fs::directory_iterator(templatePath)) {
                string name = entry.path().filename().string();
                if (name == "build" || name == "bin") {
                    continue;  // Skip
                }
                fs::copy(entry.path(), destPath / entry.path().filename(),
                         fs::copy_options::recursive);
            }
        } else {
            log("Updating existing project...");
        }

        log("Configuring CMakeLists.txt...");

        // Read CMakeLists.txt from template and write to project
        string templateCmakePath = templatePath + "/CMakeLists.txt";
        ifstream inFile(templateCmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();

        string content = buffer.str();

        // Replace TRUSSC_DIR (relative if inside tcRoot, absolute otherwise)
        size_t pos = content.find("set(TRUSSC_DIR \"");
        if (pos != string::npos) {
            size_t endPos = content.find("\")", pos);
            if (endPos != string::npos) {
                content.replace(pos, endPos - pos + 2,
                    "set(TRUSSC_DIR \"" + getTrusscDirValue(destPath) + "\")");
            }
        }

        // Write to project
        string cmakePath = destPath + "/CMakeLists.txt";
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        log("Creating addons.make...");

        // Generate addons.make
        string addonsMakePath = destPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // Generate IDE-specific files
        if (ideType == IdeType::VSCode || ideType == IdeType::Cursor) {
            log("Generating VSCode/Cursor files...");
            generateVSCodeFiles(destPath);
        } else if (ideType == IdeType::Xcode) {
            log("Generating Xcode project (this may take a while)...");
            generateXcodeProject(destPath);
        } else if (ideType == IdeType::VisualStudio) {
            log("Generating Visual Studio project (this may take a while)...");
            generateVisualStudioProject(destPath);
        }

        // Generate Web build files
        if (generateWebBuild) {
            log("Generating Web build files...");
            generateWebBuildFiles(destPath);
        }

        log("Done!");
        saveConfig();

        // Auto-switch to Import state after generation
        isImportedProject = true;
        importedProjectPath = destPath;

        setStatus("Project created successfully!");
        redraw();  // Redraw on completion

    } catch (const exception& e) {
        log(string("Error: ") + e.what());
        setStatus(string("Error: ") + e.what(), true);
        redraw();  // Redraw on error
    }
}

void tcApp::doUpdateProject() {
    auto log = [this](const string& msg) {
        lock_guard<mutex> lock(logMutex);
        generatingLog += msg + "\n";
        redraw();
    };

    if (!isImportedProject || importedProjectPath.empty()) {
        log("Error: No project imported");
        setStatus("No project imported", true);
        return;
    }

    string cmakePath = importedProjectPath + "/CMakeLists.txt";

    try {
        log("Reading template...");

        // Read new CMakeLists.txt from template (always overwrite)
        string templatePath = getTemplatePath();
        string templateCmakePath = templatePath + "/CMakeLists.txt";

        ifstream inFile(templateCmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();
        string content = buffer.str();

        log("Configuring CMakeLists.txt...");

        // Replace TRUSSC_DIR (relative if inside tcRoot, absolute otherwise)
        size_t pos = content.find("set(TRUSSC_DIR \"");
        if (pos != string::npos) {
            size_t endPos = content.find("\")", pos);
            if (endPos != string::npos) {
                content.replace(pos, endPos - pos + 2,
                    "set(TRUSSC_DIR \"" + getTrusscDirValue(importedProjectPath) + "\")");
            }
        }

        // Write back
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        log("Updating addons.make...");

        // Update addons.make
        string addonsMakePath = importedProjectPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // Copy .gitignore if it doesn't exist
        string gitignorePath = importedProjectPath + "/.gitignore";
        if (!fs::exists(gitignorePath)) {
            log("Adding .gitignore...");
            string templateGitignore = getTemplatePath() + "/.gitignore";
            if (fs::exists(templateGitignore)) {
                fs::copy_file(templateGitignore, gitignorePath);
            }
        }

        // Generate IDE-specific files
        if (ideType == IdeType::VSCode || ideType == IdeType::Cursor) {
            log("Generating VSCode/Cursor files...");
            generateVSCodeFiles(importedProjectPath);
        } else if (ideType == IdeType::Xcode) {
            log("Generating Xcode project (this may take a while)...");
            generateXcodeProject(importedProjectPath);
        } else if (ideType == IdeType::VisualStudio) {
            log("Generating Visual Studio project (this may take a while)...");
            generateVisualStudioProject(importedProjectPath);
        }

        // Generate Web build files
        if (generateWebBuild) {
            log("Generating Web build files...");
            generateWebBuildFiles(importedProjectPath);
        }

        log("Done!");
        setStatus("Project updated successfully!");
        redraw();  // Redraw on completion

    } catch (const exception& e) {
        log(string("Error: ") + e.what());
        setStatus(string("Error: ") + e.what(), true);
        redraw();  // Redraw on error
    }
}

void tcApp::generateWebBuildFiles(const string& path) {
    // build-web.sh (macOS / Linux)
    string shPath = path + "/build-web.sh";
    ofstream shFile(shPath);
    shFile << "#!/bin/bash\n";
    shFile << "# TrussC Web Build Script (Emscripten)\n";
    shFile << "# Requires: Emscripten SDK (https://emscripten.org/docs/getting_started/downloads.html)\n";
    shFile << "\n";
    shFile << "set -e\n";
    shFile << "\n";
    shFile << "# Create build directory\n";
    shFile << "mkdir -p build-web\n";
    shFile << "cd build-web\n";
    shFile << "\n";
    shFile << "# CMake configuration (Emscripten)\n";
    shFile << "emcmake cmake ..\n";
    shFile << "\n";
    shFile << "# Build\n";
    shFile << "cmake --build .\n";
    shFile << "\n";
    shFile << "echo \"\"\n";
    shFile << "echo \"Build complete! Output files are in bin/\"\n";
    shFile << "echo \"To test locally:\"\n";
    shFile << "echo \"  cd ../bin && python3 -m http.server 8080\"\n";
    shFile << "echo \"  Open http://localhost:8080/$(basename $(pwd)).html\"\n";
    shFile.close();

    // Set execute permission
#ifndef _WIN32
    chmod(shPath.c_str(), 0755);
#endif

    // build-web.bat (Windows)
    string batPath = path + "/build-web.bat";
    ofstream batFile(batPath);
    batFile << "@echo off\r\n";
    batFile << "REM TrussC Web Build Script (Emscripten)\r\n";
    batFile << "REM Requires: Emscripten SDK (https://emscripten.org/docs/getting_started/downloads.html)\r\n";
    batFile << "\r\n";
    batFile << "REM Create build directory\r\n";
    batFile << "if not exist build-web mkdir build-web\r\n";
    batFile << "cd build-web\r\n";
    batFile << "\r\n";
    batFile << "REM CMake configuration (Emscripten)\r\n";
    batFile << "call emcmake cmake ..\r\n";
    batFile << "if errorlevel 1 goto error\r\n";
    batFile << "\r\n";
    batFile << "REM Build\r\n";
    batFile << "cmake --build .\r\n";
    batFile << "if errorlevel 1 goto error\r\n";
    batFile << "\r\n";
    batFile << "echo.\r\n";
    batFile << "echo Build complete! Output files are in bin\\\r\n";
    batFile << "echo To test locally:\r\n";
    batFile << "echo   cd ..\\bin ^&^& python -m http.server 8080\r\n";
    batFile << "goto end\r\n";
    batFile << "\r\n";
    batFile << ":error\r\n";
    batFile << "echo Build failed!\r\n";
    batFile << "pause\r\n";
    batFile << "exit /b 1\r\n";
    batFile << "\r\n";
    batFile << ":end\r\n";
    batFile << "cd ..\r\n";
    batFile.close();
}
