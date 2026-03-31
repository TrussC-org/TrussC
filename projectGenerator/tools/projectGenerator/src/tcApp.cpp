// =============================================================================
// tcApp.cpp - TrussC Project Generator
// =============================================================================

#include "tcApp.h"
#include "ProjectGenerator.h"
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

// Check if an environment variable is set, even in GUI apps on macOS
// (GUI apps don't inherit .zshrc env vars, so we ask a login shell)
static bool hasEnvVar(const string& name) {
    if (getenv(name.c_str())) return true;
#ifdef __APPLE__
    string cmd = "/bin/zsh -c 'source ~/.zshrc 2>/dev/null; source ~/.zprofile 2>/dev/null; printenv " + name + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    char buf[16];
    bool found = fgets(buf, sizeof(buf), pipe) != nullptr;
    pclose(pipe);
    return found;
#else
    return false;
#endif
}

void tcApp::setup() {
    imguiSetup();

    // Power-saving mode: update at 30fps, draw is event-driven
    setIndependentFps(30, EVENT_DRIVEN);

    // Config file path (~/.trussc/config.json)
    string home = getenv("HOME") ? getenv("HOME") : "";
    configPath = home + "/.trussc/config.json";

    // Load config
    loadConfig();

    // Validate TC_ROOT - clear if invalid (triggers auto-detection)
    if (!tcRoot.empty() && !fs::exists(tcRoot + "/trussc/cmake/trussc_app.cmake")) {
        logNotice("tcApp") << "TC_ROOT is invalid, clearing: " << tcRoot;
        tcRoot.clear();
        tcRootBuf[0] = '\0';
    }

    // Auto-detect TC_ROOT if not set
    // Search up to 5 parent directories from executable location
    if (tcRoot.empty()) {
        fs::path exePath = platform::getExecutablePath();
        fs::path searchPath = exePath.parent_path();

        // On macOS, executable is in .app/Contents/MacOS/, go up to .app's parent
        #ifdef __APPLE__
        // Go up 3 levels: MacOS -> Contents -> .app -> parent
        for (int i = 0; i < 3 && searchPath.has_parent_path(); i++) {
            searchPath = searchPath.parent_path();
        }
        #endif

        // Search up to 5 parent directories
        for (int i = 0; i < 5 && searchPath.has_parent_path(); i++) {
            fs::path checkPath = searchPath / "trussc" / "cmake" / "trussc_app.cmake";
            if (fs::exists(checkPath)) {
                tcRoot = searchPath.string();
                strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);
                logNotice("tcApp") << "Auto-detected TC_ROOT: " << tcRoot;
                break;
            }
            searchPath = searchPath.parent_path();
        }
    }

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

    // Auto-import if previous imported path exists
    if (!importedProjectPath.empty() && fs::is_directory(importedProjectPath)) {
        importProject(importedProjectPath);
    }

    // プラットフォーム固有のビルド環境を検出（ProjectSettings::detectBuildEnvironment と同じ処理）
    // GUI側はUI表示用にメンバ変数にも保持する
    {
        ProjectSettings tmp;
        tmp.detectBuildEnvironment();
        installedVsVersions = tmp.installedVsVersions;
        selectedVsIndex = tmp.selectedVsIndex;
    }

    // Check Android env (shell-aware on macOS for GUI apps)
    {
        bool a = hasEnvVar("ANDROID_HOME");
        bool j = hasEnvVar("JAVA_HOME");
        androidEnvOk = a && j;
        if (!a) androidEnvTip += "ANDROID_HOME is not set\n";
        if (!j) androidEnvTip += "JAVA_HOME is not set";
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
void tcApp::mousePressed(Vec2 pos, int button) {
    redraw();
}

void tcApp::mouseReleased(Vec2 pos, int button) {
    redraw();
}

void tcApp::mouseMoved(Vec2 pos) {
    // Redraw only when inside the window
    if (pos.x >= 0 && pos.x < getWindowWidth() && pos.y >= 0 && pos.y < getWindowHeight()) {
        redraw();
    }
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    redraw();
}

void tcApp::mouseScrolled(Vec2 delta) {
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

    // Check if it's a directory
    if (fs::is_directory(path)) {
        if (showSetupDialog) {
            // In setup dialog: set as TC_ROOT
            strncpy(tcRootBuf, path.c_str(), sizeof(tcRootBuf) - 1);
        } else {
            // In main window: import as project
            importProject(path);
        }
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
            auto result = loadDialog("Select TrussC folder", "", "", true);
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
            if (!tcRoot.empty() && fs::exists(tcRoot + "/trussc/cmake/trussc_app.cmake")) {
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

    // Mode selection buttons (Import / New)
    {
        float buttonWidth = (ImGui::GetContentRegionAvail().x - 8) / 2;
        float buttonHeight = 32;

        // Import button
        bool isImportMode = (mode == ProjectMode::Import);
        if (isImportMode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        }
        if (ImGui::Button("Import Existing", ImVec2(buttonWidth, buttonHeight))) {
            mode = ProjectMode::Import;
            // Restore hasImportedProject if path is still valid
            if (!importedProjectPath.empty() && fs::is_directory(importedProjectPath)) {
                hasImportedProject = true;
            }
            setStatus("");
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        // New button
        bool isNewMode = (mode == ProjectMode::New);
        if (isNewMode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        }
        if (ImGui::Button("Create New", ImVec2(buttonWidth, buttonHeight))) {
            mode = ProjectMode::New;
            hasImportedProject = false;
            setStatus("");
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Mode-specific UI
    if (mode == ProjectMode::Import) {
        // Import mode: show project path
        ImGui::Text("Project");
        ImGui::SetNextItemWidth(-80);
        // Show full path (read-only display)
        char importPathBuf[512] = "";
        strncpy(importPathBuf, importedProjectPath.c_str(), sizeof(importPathBuf) - 1);
        ImGui::BeginDisabled();
        ImGui::InputText("##importPath", importPathBuf, sizeof(importPathBuf));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Browse##import")) {
            auto result = loadDialog("Select existing project", "", "", true);
            if (result.success) {
                importProject(result.filePath);
            }
            redraw(2);
        }

        // Show project name if imported
        if (hasImportedProject) {
            ImGui::Spacing();
            ImGui::TextDisabled("Project Name: %s", projectName.c_str());
        }
    } else {
        // New mode: show location + name
        ImGui::Text("Location");
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##projectDir", projectDirBuf, sizeof(projectDirBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##dir")) {
            auto result = loadDialog("Select project location", "", "", true);
            if (result.success) {
                strncpy(projectDirBuf, result.filePath.c_str(), sizeof(projectDirBuf) - 1);
                projectDir = projectDirBuf;
                saveConfig();
            }
            redraw(2);
        }

        ImGui::Spacing();

        ImGui::Text("Project Name");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##projectName", projectNameBuf, sizeof(projectNameBuf));
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

    // IDE selection (OS-specific options)
    ImGui::Text("IDE");
    ImGui::SetNextItemWidth(-1);
#ifdef __APPLE__
    // macOS: CMake only, VSCode, Cursor, Xcode
    const char* ideItems[] = { "CMake only", "VSCode", "Cursor", "Xcode" };
    int displayIndex = static_cast<int>(ideType);
    if (ideType == IdeType::VisualStudio) {
        displayIndex = 0;  // Fallback
        ideType = IdeType::CMakeOnly;
    }
    if (ImGui::Combo("##ide", &displayIndex, ideItems, 4)) {
        ideType = static_cast<IdeType>(displayIndex);
        saveConfig();
    }
#elif defined(_WIN32)
    // Windows: CMake only, VSCode, Cursor, Visual Studio (detected version)
    string vsName = installedVsVersions.empty() ? "Visual Studio" : installedVsVersions[0].displayName;
    const char* ideItems[] = { "CMake only", "VSCode", "Cursor", vsName.c_str() };
    int displayIndex;
    if (ideType == IdeType::VisualStudio) {
        displayIndex = 3;
    } else if (ideType == IdeType::Xcode) {
        displayIndex = 0;  // Fallback
        ideType = IdeType::CMakeOnly;
    } else {
        displayIndex = static_cast<int>(ideType);
    }
    if (ImGui::Combo("##ide", &displayIndex, ideItems, 4)) {
        if (displayIndex == 3) {
            ideType = IdeType::VisualStudio;
        } else {
            ideType = static_cast<IdeType>(displayIndex);
        }
        saveConfig();
    }
#else
    // Linux: CMake only, VSCode, Cursor
    const char* ideItems[] = { "CMake only", "VSCode", "Cursor" };
    int displayIndex = static_cast<int>(ideType);
    if (displayIndex > 2) {
        displayIndex = 0;  // Fallback
        ideType = IdeType::CMakeOnly;
    }
    if (ImGui::Combo("##ide", &displayIndex, ideItems, 3)) {
        ideType = static_cast<IdeType>(displayIndex);
        saveConfig();
    }
#endif

    ImGui::Spacing();

    // Cross-compile targets (collapsible)
    if (ImGui::CollapsingHeader("Cross-compile targets")) {
        ImGui::Indent(8);

        // Android
        if (ImGui::Checkbox("Android (beta)", &generateAndroidBuild)) {
            saveConfig();
        }
        if (generateAndroidBuild && !androidEnvOk) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(!)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", androidEnvTip.c_str());
            }
        }

        // iOS (macOS only)
#ifdef __APPLE__
        if (ImGui::Checkbox("iOS (beta)", &generateIosBuild)) {
            saveConfig();
        }
#endif

        // Web
        if (ImGui::Checkbox("Web", &generateWebBuild)) {
            saveConfig();
        }
        if (generateWebBuild) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            const char* webBackendItems[] = { "WebGPU", "WebGL" };
            if (ImGui::Combo("##webBackend", &webBackend, webBackendItems, 2)) {
                saveConfig();
            }
        }

        ImGui::Unindent(8);
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
    } else if (mode == ProjectMode::Import && hasImportedProject) {
        // Import mode with project loaded: Update and Open in IDE
        float buttonWidth = (ImGui::GetContentRegionAvail().x - 8) / 2;
        if (ImGui::Button("Update Project", ImVec2(buttonWidth, 40))) {
            startUpdate();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open in IDE", ImVec2(buttonWidth, 40))) {
            string err = IdeHelper::openInIde(ideType, importedProjectPath);
            if (!err.empty()) {
                setStatus(err, true);
            }
        }
    } else if (mode == ProjectMode::New) {
        // New mode: Generate button
        if (ImGui::Button("Generate Project", ImVec2(-1, 40))) {
            projectName = projectNameBuf;
            projectDir = projectDirBuf;

            // Check if folder already exists
            string destPath = projectDir + "/" + projectName;
            if (fs::is_directory(destPath)) {
                setStatus("Folder already exists. Use 'Import Existing' to update.", true);
            } else {
                startGenerate();
            }
        }
    } else {
        // Import mode but no project selected
        ImGui::BeginDisabled();
        ImGui::Button("Select a project to update", ImVec2(-1, 40));
        ImGui::EndDisabled();
    }

    // Generation log display (clickable to copy)
    if (isGenerating || !generatingLog.empty()) {
        ImGui::Spacing();
        ImGui::BeginChild("##log", ImVec2(0, 85), true);
        string logCopy;
        {
            lock_guard<mutex> lock(logMutex);
            logCopy = generatingLog;
        }
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 10);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", logCopy.c_str());
        ImGui::PopTextWrapPos();

        // Click to copy log
        if (ImGui::IsItemClicked() && !logCopy.empty()) {
            setClipboardString(logCopy);
            showCopiedPopup = true;
            callAfter(2.0, [this]() {
                showCopiedPopup = false;
                redraw();
            });
        }

        // Auto-scroll to bottom
        if (isGenerating) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    // Status message (clickable to copy)
    if (!statusMessage.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 10);
        ImVec4 color = statusIsError ? ImVec4(1, 0.4f, 0.4f, 1) : ImVec4(0.4f, 1, 0.4f, 1);
        ImGui::TextColored(color, "%s", statusMessage.c_str());
        ImGui::PopTextWrapPos();

        // Click to copy
        if (ImGui::IsItemClicked()) {
            setClipboardString(statusMessage);
            showCopiedPopup = true;
            callAfter(2.0, [this]() {
                showCopiedPopup = false;
                redraw();
            });
        }
    }

    // Copied popup
    if (showCopiedPopup) {
        ImGui::BeginTooltip();
        ImGui::Text("Copied!");
        ImGui::EndTooltip();
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
    logNotice("tcApp") << "cleanup: saving projectName=" << projectName << ", projectDir=" << projectDir;
    saveConfig();

    imguiShutdown();
}

void tcApp::loadConfig() {
    logNotice("tcApp") << "loadConfig: configPath = " << configPath;
    if (!fs::exists(configPath)) {
        logNotice("tcApp") << "loadConfig: config file not found";
        return;
    }

    Json config = loadJson(configPath);
    if (config.empty()) {
        logNotice("tcApp") << "loadConfig: config is empty";
        return;
    }

    // tc_root is NOT loaded from config (always auto-detect or select manually)
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
    if (config.contains("generate_android_build")) {
        generateAndroidBuild = config["generate_android_build"].get<bool>();
    }
    if (config.contains("generate_ios_build")) {
        generateIosBuild = config["generate_ios_build"].get<bool>();
    }
    if (config.contains("web_backend")) {
        webBackend = config["web_backend"].get<int>();
    }
    if (config.contains("last_imported_path")) {
        importedProjectPath = config["last_imported_path"].get<string>();
    }
    logNotice("tcApp") << "loadConfig: projectDir = " << projectDir << ", projectName = " << projectName;
}

void tcApp::saveConfig() {
    // Create ~/.trussc/ directory
    fs::path configDir = fs::path(configPath).parent_path();
    if (!fs::exists(configDir)) {
        fs::create_directories(configDir);
    }

    Json config;
    // tc_root is NOT saved (always auto-detect on startup)
    config["last_project_dir"] = projectDir;
    config["last_project_name"] = projectName;
    config["ide_type"] = static_cast<int>(ideType);
    config["generate_web_build"] = generateWebBuild;
    config["generate_android_build"] = generateAndroidBuild;
    config["generate_ios_build"] = generateIosBuild;
    config["web_backend"] = webBackend;
    config["last_imported_path"] = importedProjectPath;
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

                // Update tcRoot if valid path
                if (!importedTcRoot.empty() && fs::exists(importedTcRoot + "/trussc/cmake/trussc_app.cmake")) {
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
    mode = ProjectMode::Import;
    hasImportedProject = true;
    importedProjectPath = path;
    setStatus("Project imported: " + projectName);
}

void tcApp::resetToNewProject() {
    mode = ProjectMode::New;
    hasImportedProject = false;
    // Keep projectName and projectDir (don't reset)

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

ProjectSettings tcApp::buildProjectSettings() {
    ProjectSettings s;
    s.projectName = projectName;
    s.projectDir = projectDir;
    s.tcRoot = tcRoot;
    s.templatePath = getTemplatePath();
    s.addons = addons;
    s.addonSelected = addonSelected;
    s.ideType = ideType;
    s.generateWebBuild = generateWebBuild;
    s.generateAndroidBuild = generateAndroidBuild;
    s.generateIosBuild = generateIosBuild;
    s.webBackend = webBackend;
    s.detectBuildEnvironment();
    return s;
}

void tcApp::doGenerateProject() {
    auto settings = buildProjectSettings();

    ProjectGenerator generator(settings);

    // Set log callback for progress messages
    generator.setLogCallback([this](const string& msg) {
        lock_guard<mutex> lock(logMutex);
        generatingLog += msg + "\n";
        redraw();
    });

    // Generate project
    string error = generator.generate();
    if (!error.empty()) {
        setStatus(error, true);
        redraw();
        return;
    }

    saveConfig();

    // Auto-switch to Import mode after generation
    mode = ProjectMode::Import;
    hasImportedProject = true;
    importedProjectPath = generator.getDestPath();

    setStatus("Project created successfully!");
    redraw();
}

void tcApp::doUpdateProject() {
    if (!hasImportedProject || importedProjectPath.empty()) {
        lock_guard<mutex> lock(logMutex);
        generatingLog += "Error: No project imported\n";
        setStatus("No project imported", true);
        return;
    }

    auto settings = buildProjectSettings();

    ProjectGenerator generator(settings);

    // Set log callback for progress messages
    generator.setLogCallback([this](const string& msg) {
        lock_guard<mutex> lock(logMutex);
        generatingLog += msg + "\n";
        redraw();
    });

    // Update project
    string error = generator.update(importedProjectPath);
    if (!error.empty()) {
        setStatus(error, true);
        redraw();
        return;
    }

    setStatus("Project updated successfully!");
    redraw();
}
