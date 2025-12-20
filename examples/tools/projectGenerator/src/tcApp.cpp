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

    // 省電力モード: update は 30fps、draw はイベント駆動
    setUpdateFps(30);
    setDrawFps(0);  // 自動描画停止

    // 設定ファイルパス (~/.trussc/config.json)
    string home = getenv("HOME") ? getenv("HOME") : "";
    configPath = home + "/.trussc/config.json";

    // 設定を読み込み
    loadConfig();

    // TC_ROOT が設定されていない場合はダイアログを表示
    if (tcRoot.empty()) {
        showSetupDialog = true;
    } else {
        strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);
        scanAddons();
    }

    // デフォルトの保存先
    if (projectDir.empty()) {
        projectDir = home + "/Projects";
    }
    strncpy(projectDirBuf, projectDir.c_str(), sizeof(projectDirBuf) - 1);

    // 前回のプロジェクトが既存なら自動で Update モードに
    if (!projectDir.empty() && !projectName.empty()) {
        string lastProjectPath = projectDir + "/" + projectName;
        if (fs::exists(lastProjectPath + "/CMakeLists.txt")) {
            importProject(lastProjectPath);
        }
    }

    // 初回描画
    redraw();
}

void tcApp::update() {
    // 生成中は毎フレーム再描画（明滅アニメーション用）
    if (isGenerating) {
        redraw();
    }
}

// マウスイベントで再描画
void tcApp::mousePressed(int x, int y, int button) {
    redraw();
}

void tcApp::mouseReleased(int x, int y, int button) {
    redraw();
}

void tcApp::mouseMoved(int x, int y) {
    // 画面内の時だけ再描画
    if (x >= 0 && x < getWindowWidth() && y >= 0 && y < getWindowHeight()) {
        redraw();
    }
}

void tcApp::mouseDragged(int x, int y, int button) {
    redraw();
}

// キーイベントで再描画
void tcApp::keyPressed(int key) {
    redraw();
}

void tcApp::keyReleased(int key) {
    redraw();
}

void tcApp::draw() {
    clear(45, 45, 48);

    imguiBegin();

    // TC_ROOT 設定ダイアログ
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

        ImGui::TextWrapped("Please select the TrussC folder (e.g. tc_v0.0.1).");
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::Text("TrussC Folder");
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##tcRoot", tcRootBuf, sizeof(tcRootBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            auto result = loadDialog("Select TrussC folder (tc_vX.Y.Z)", true);
            if (result.success) {
                strncpy(tcRootBuf, result.filePath.c_str(), sizeof(tcRootBuf) - 1);
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (ImGui::Button("OK", ImVec2(120, 30))) {
            tcRoot = tcRootBuf;
            // tc_vX.Y.Z フォルダか確認（CMakeLists.txt があるか）
            if (!tcRoot.empty() && fs::exists(tcRoot + "/CMakeLists.txt")) {
                showSetupDialog = false;
                saveConfig();
                scanAddons();
            } else {
                setStatus("Invalid TrussC folder (CMakeLists.txt not found)", true);
            }
        }

        // ステータスメッセージ
        if (!statusMessage.empty() && statusIsError) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", statusMessage.c_str());
        }

        ImGui::End();
        imguiEnd();
        return;  // メインウィンドウは描画しない
    }

    // メインウィンドウ（ウィンドウ全体に固定）
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(getWindowWidth(), getWindowHeight()));
    ImGui::Begin("TrussC Project Generator", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    // プロジェクト名
    ImGui::Text("Project Name");
    ImGui::SetNextItemWidth(-80);
    if (isImportedProject) {
        ImGui::BeginDisabled();
    }
    ImGui::InputText("##projectName", projectNameBuf, sizeof(projectNameBuf));
    if (isImportedProject) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        auto result = loadDialog("Select existing project", true);
        if (result.success) {
            importProject(result.filePath);
        }
    }

    ImGui::Spacing();

    // 保存先
    ImGui::Text("Location");
    ImGui::SetNextItemWidth(-80);
    if (isImportedProject) {
        ImGui::BeginDisabled();
    }
    ImGui::InputText("##projectDir", projectDirBuf, sizeof(projectDirBuf));
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
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // アドオン選択
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

    // IDE 選択
    ImGui::Text("IDE");
    ImGui::SetNextItemWidth(-1);
    const char* ideItems[] = { "CMake only", "VSCode", "Cursor", "Xcode (macOS)", "Visual Studio (Windows)" };
    int ideIndex = static_cast<int>(ideType);
    if (ImGui::Combo("##ide", &ideIndex, ideItems, 5)) {
        ideType = static_cast<IdeType>(ideIndex);
        saveConfig();  // IDE 変更時に保存
    }

    ImGui::Spacing();

    // Web ビルド オプション
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

    // 生成/更新ボタン
    if (isGenerating) {
        // 生成中: 明滅するボタン
        float t = getElapsedTime();
        float pulse = 0.5f + 0.3f * sinf(t * 4.0f);  // 明滅
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, pulse));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.8f, pulse));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.8f, pulse));
        ImGui::Button("Generating...", ImVec2(-1, 40));
        ImGui::PopStyleColor(3);
    } else if (isImportedProject) {
        // Update と Open in IDE を横並びに
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

    // 生成ログ表示
    if (isGenerating || !generatingLog.empty()) {
        ImGui::Spacing();
        ImGui::BeginChild("##log", ImVec2(0, 85), true);
        lock_guard<mutex> lock(logMutex);
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 10);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", generatingLog.c_str());
        ImGui::PopTextWrapPos();
        // 自動スクロール（一番下へ）
        if (isGenerating) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    // ステータスメッセージ（ウィンドウ幅-10pxで折り返し）
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

    // TC_ROOT 設定ボタン（下部）
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
    // 終了時に現在の状態を保存
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
    // ~/.trussc/ ディレクトリを作成
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

    // アドオンフォルダを列挙
    for (const auto& entry : fs::directory_iterator(addonsPath)) {
        if (entry.is_directory()) {
            string name = entry.path().filename().string();
            if (name.substr(0, 3) == "tcx") {
                addons.push_back(name);
                addonSelected.push_back(0);
            }
        }
    }

    // ソート
    sort(addons.begin(), addons.end());
}

string tcApp::getTemplatePath() {
    if (tcRoot.empty()) return "";
    return tcRoot + "/examples/templates/emptyExample";
}

bool tcApp::generateProject() {
    // バリデーション
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

    // プロジェクトパス（末尾のスラッシュを除去）
    while (!projectDir.empty() && projectDir.back() == '/') {
        projectDir.pop_back();
    }
    string destPath = projectDir + "/" + projectName;

    // 既存チェック
    if (fs::exists(destPath)) {
        setStatus("Project already exists", true);
        return false;
    }

    try {
        // 親ディレクトリを作成
        fs::create_directories(projectDir);

        // テンプレートをコピー（build, bin フォルダは除外）
        fs::create_directories(destPath);
        for (const auto& entry : fs::directory_iterator(templatePath)) {
            string name = entry.path().filename().string();
            if (name == "build" || name == "bin") {
                continue;  // スキップ
            }
            fs::copy(entry.path(), destPath / entry.path().filename(),
                     fs::copy_options::recursive);
        }

        // CMakeLists.txt を書き換え（TC_ROOT を直接設定）
        string cmakePath = destPath + "/CMakeLists.txt";
        ifstream inFile(cmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();

        string content = buffer.str();

        // TC_ROOT を直接設定
        size_t pos = content.find("set(TC_ROOT \"\"");
        if (pos != string::npos) {
            content.replace(pos, 14, "set(TC_ROOT \"" + tcRoot + "\"");
        }

        // 書き戻し
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        // addons.make を生成
        string addonsMakePath = destPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // IDE 固有のファイル生成
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
    // .vscode フォルダを作成
    string vscodePath = path + "/.vscode";
    fs::create_directories(vscodePath);

    // launch.json (OS ごとに分岐)
    Json launch;
    launch["version"] = "0.2.0";
    launch["configurations"] = Json::array();

    Json config;
    config["name"] = "Debug";
    config["type"] = "lldb";
    config["request"] = "launch";
    config["cwd"] = "${workspaceFolder}";
    config["preLaunchTask"] = "CMake: build";

    // OS ごとのプログラムパス
    Json osx;
    osx["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.app/Contents/MacOS/${workspaceFolderBasename}";
    config["osx"] = osx;

    Json linux_cfg;
    linux_cfg["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}";
    config["linux"] = linux_cfg;

    Json windows;
    windows["program"] = "${workspaceFolder}/bin/${workspaceFolderBasename}.exe";
    windows["type"] = "cppvsdbg";  // Windows では cppvsdbg を使用
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
}

void tcApp::generateXcodeProject(const string& path) {
    // build フォルダを削除して cmake -G Xcode を実行
    // （既存の CMakeCache があると Xcode ジェネレータに切り替えられないため）
    string buildPath = path + "/build";
    if (fs::exists(buildPath)) {
        fs::remove_all(buildPath);
    }
    fs::create_directories(buildPath);

    // GUI アプリから実行すると PATH が通ってないので cmake のフルパスを使用
    // TC_ROOT は CMakeLists.txt に直接書いてあるので環境変数不要
    string cmd = "cd \"" + buildPath + "\" && /opt/homebrew/bin/cmake -G Xcode ..";
    tcLogNotice("tcApp") << "Xcode cmd: " << cmd;
    int result = system(cmd.c_str());

    if (result != 0) {
        tcLogWarning("tcApp") << "Failed to generate Xcode project (exit code: " << result << ")";
        return;
    }

    // Debug と Release の2つのスキームを生成
    generateXcodeSchemes(path);
}

void tcApp::generateXcodeSchemes(const string& path) {
    tcLogNotice("tcApp") << "generateXcodeSchemes called with path: " << path;

    string buildPath = path + "/build";
    string projectName = fs::path(path).filename().string();
    tcLogNotice("tcApp") << "buildPath: " << buildPath << ", projectName: " << projectName;

    // .xcodeproj を探す
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

    // スキームディレクトリ
    string schemesDir = xcodeprojPath + "/xcshareddata/xcschemes";
    if (!fs::exists(schemesDir)) return;

    // 元のスキームファイルを探す
    string originalScheme;
    for (const auto& entry : fs::directory_iterator(schemesDir)) {
        if (entry.path().extension() == ".xcscheme") {
            originalScheme = entry.path().string();
            break;
        }
    }
    if (originalScheme.empty()) return;

    // スキームの内容を読み込む
    ifstream schemeFile(originalScheme);
    if (!schemeFile.is_open()) return;
    stringstream buffer;
    buffer << schemeFile.rdbuf();
    string schemeContent = buffer.str();
    schemeFile.close();

    // 元のスキームを削除
    fs::remove(originalScheme);

    // Debug スキームを生成（デフォルトの buildConfiguration = "Debug" のまま）
    string debugScheme = schemesDir + "/" + projectName + " Debug.xcscheme";
    ofstream debugFile(debugScheme);
    debugFile << schemeContent;
    debugFile.close();

    // Release スキームを生成（buildConfiguration を RelWithDebInfo に変更）
    string releaseContent = schemeContent;
    // 全ての buildConfiguration="Debug" を RelWithDebInfo に変更
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
    // build フォルダを削除して cmake -G "Visual Studio 17 2022" を実行
    string buildPath = path + "/build";
    if (fs::exists(buildPath)) {
        fs::remove_all(buildPath);
    }
    fs::create_directories(buildPath);

#ifdef _WIN32
    string cmd = "cd /d \"" + buildPath + "\" && cmake -G \"Visual Studio 17 2022\" ..";
#else
    // macOS/Linux からは生成のみ（開くのは Windows で）
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
            // build/*.xcodeproj を探して開く
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
            // ターミナルで開く
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
            // build/*.sln を探して開く
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
    // CMakeLists.txt が存在するか確認
    string cmakePath = path + "/CMakeLists.txt";
    if (!fs::exists(cmakePath)) {
        setStatus("Not a valid TrussC project (CMakeLists.txt not found)", true);
        return;
    }

    // CMakeLists.txt を解析
    ifstream inFile(cmakePath);
    stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();
    string content = buffer.str();

    // プロジェクト名を取得（フォルダ名）
    projectName = fs::path(path).filename().string();
    strncpy(projectNameBuf, projectName.c_str(), sizeof(projectNameBuf) - 1);

    // 保存先を設定
    projectDir = fs::path(path).parent_path().string();
    strncpy(projectDirBuf, projectDir.c_str(), sizeof(projectDirBuf) - 1);

    // TC_ROOT を解析
    // 形式: set(TC_ROOT "/path/to/tc_v0.0.1"
    size_t pos = content.find("set(TC_ROOT \"");
    if (pos != string::npos) {
        size_t start = pos + 13;
        size_t end = content.find("\"", start);
        if (end != string::npos) {
            string importedTcRoot = content.substr(start, end - start);
            // 有効なパスなら tcRoot を更新
            if (!importedTcRoot.empty() && fs::exists(importedTcRoot + "/CMakeLists.txt")) {
                tcRoot = importedTcRoot;
                strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);
                saveConfig();
                scanAddons();
            }
        }
    }

    // addons.make からアドオンを読み込み
    for (size_t i = 0; i < addons.size(); i++) {
        addonSelected[i] = 0;
    }

    string addonsMakePath = path + "/addons.make";
    if (fs::exists(addonsMakePath)) {
        ifstream addonsFile(addonsMakePath);
        string line;
        while (getline(addonsFile, line)) {
            // 空白を除去
            size_t start = line.find_first_not_of(" \t");
            if (start == string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            string addonName = line.substr(start, end - start + 1);

            // コメント行をスキップ
            if (addonName.empty() || addonName[0] == '#') continue;

            // アドオンリストから探して選択
            for (size_t i = 0; i < addons.size(); i++) {
                if (addons[i] == addonName) {
                    addonSelected[i] = 1;
                    break;
                }
            }
        }
        addonsFile.close();
    }

    // インポート状態を設定
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
    if (!fs::exists(cmakePath)) {
        setStatus("CMakeLists.txt not found", true);
        return false;
    }

    try {
        // テンプレートから新しい CMakeLists.txt を読み込み
        string templatePath = getTemplatePath();
        string templateCmakePath = templatePath + "/CMakeLists.txt";

        ifstream inFile(templateCmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();
        string content = buffer.str();

        // TC_ROOT を直接設定
        size_t pos = content.find("set(TC_ROOT \"\"");
        if (pos != string::npos) {
            content.replace(pos, 14, "set(TC_ROOT \"" + tcRoot + "\"");
        }

        // 書き戻し
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        // addons.make を更新
        string addonsMakePath = importedProjectPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // IDE 固有のファイル生成
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
    // projectName は前回の値を維持（リセットしない）

    // アドオン選択をリセット
    for (size_t i = 0; i < addonSelected.size(); i++) {
        addonSelected[i] = 0;
    }

    setStatus("");
}

void tcApp::startGenerate() {
    if (isGenerating) return;

    // 末尾のスラッシュを除去（2文字以上かつ末尾が / の場合）
    while (projectDir.size() > 1 && projectDir.back() == '/') {
        projectDir.pop_back();
    }
    strncpy(projectDirBuf, projectDir.c_str(), sizeof(projectDirBuf) - 1);

    isGenerating = true;
    setStatus("");  // 前回のステータスをクリア
    {
        lock_guard<mutex> lock(logMutex);
        generatingLog = "Starting project generation...";
    }

    // スレッドで実行
    thread([this]() {
        doGenerateProject();
        isGenerating = false;
    }).detach();
}

void tcApp::startUpdate() {
    if (isGenerating) return;

    // 末尾のスラッシュを除去（2文字以上かつ末尾が / の場合）
    while (tcRoot.size() > 1 && tcRoot.back() == '/') {
        tcRoot.pop_back();
    }
    strncpy(tcRootBuf, tcRoot.c_str(), sizeof(tcRootBuf) - 1);

    isGenerating = true;
    setStatus("");  // 前回のステータスをクリア
    {
        lock_guard<mutex> lock(logMutex);
        generatingLog = "Starting project update...";
    }

    // スレッドで実行
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

    // バリデーション
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

    // プロジェクトパス（末尾のスラッシュを除去）
    string projDir = projectDir;
    while (!projDir.empty() && projDir.back() == '/') {
        projDir.pop_back();
    }
    string destPath = projDir + "/" + projectName;

    // 既存チェック
    if (fs::exists(destPath)) {
        log("Error: Project already exists");
        setStatus("Project already exists", true);
        return;
    }

    try {
        log("Creating project directory...");

        // 親ディレクトリを作成
        fs::create_directories(projDir);

        // テンプレートをコピー（build, bin フォルダは除外）
        fs::create_directories(destPath);
        for (const auto& entry : fs::directory_iterator(templatePath)) {
            string name = entry.path().filename().string();
            if (name == "build" || name == "bin") {
                continue;  // スキップ
            }
            fs::copy(entry.path(), destPath / entry.path().filename(),
                     fs::copy_options::recursive);
        }

        log("Configuring CMakeLists.txt...");

        // CMakeLists.txt を書き換え（TC_ROOT を直接設定）
        string cmakePath = destPath + "/CMakeLists.txt";
        ifstream inFile(cmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();

        string content = buffer.str();

        // TC_ROOT を直接設定
        size_t pos = content.find("set(TC_ROOT \"\"");
        if (pos != string::npos) {
            content.replace(pos, 14, "set(TC_ROOT \"" + tcRoot + "\"");
        }

        // 書き戻し
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        log("Creating addons.make...");

        // addons.make を生成
        string addonsMakePath = destPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // IDE 固有のファイル生成
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

        // Web ビルドファイル生成
        if (generateWebBuild) {
            log("Generating Web build files...");
            generateWebBuildFiles(destPath);
        }

        log("Done!");
        saveConfig();

        // 生成完了後、自動的に Import 状態に
        isImportedProject = true;
        importedProjectPath = destPath;

        setStatus("Project created successfully!");
        redraw();  // 完了時に再描画

    } catch (const exception& e) {
        log(string("Error: ") + e.what());
        setStatus(string("Error: ") + e.what(), true);
        redraw();  // エラー時も再描画
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
    if (!fs::exists(cmakePath)) {
        log("Error: CMakeLists.txt not found");
        setStatus("CMakeLists.txt not found", true);
        return;
    }

    try {
        log("Reading template...");

        // テンプレートから新しい CMakeLists.txt を読み込み
        string templatePath = getTemplatePath();
        string templateCmakePath = templatePath + "/CMakeLists.txt";

        ifstream inFile(templateCmakePath);
        stringstream buffer;
        buffer << inFile.rdbuf();
        inFile.close();
        string content = buffer.str();

        log("Configuring CMakeLists.txt...");

        // TC_ROOT を直接設定
        size_t pos = content.find("set(TC_ROOT \"\"");
        if (pos != string::npos) {
            content.replace(pos, 14, "set(TC_ROOT \"" + tcRoot + "\"");
        }

        // 書き戻し
        ofstream outFile(cmakePath);
        outFile << content;
        outFile.close();

        log("Updating addons.make...");

        // addons.make を更新
        string addonsMakePath = importedProjectPath + "/addons.make";
        ofstream addonsFile(addonsMakePath);
        addonsFile << "# TrussC addons - one addon per line\n";
        for (size_t i = 0; i < addons.size(); i++) {
            if (addonSelected[i]) {
                addonsFile << addons[i] << "\n";
            }
        }
        addonsFile.close();

        // IDE 固有のファイル生成
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

        // Web ビルドファイル生成
        if (generateWebBuild) {
            log("Generating Web build files...");
            generateWebBuildFiles(importedProjectPath);
        }

        log("Done!");
        setStatus("Project updated successfully!");
        redraw();  // 完了時に再描画

    } catch (const exception& e) {
        log(string("Error: ") + e.what());
        setStatus(string("Error: ") + e.what(), true);
        redraw();  // エラー時も再描画
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
    shFile << "# ビルドディレクトリ作成\n";
    shFile << "mkdir -p build-web\n";
    shFile << "cd build-web\n";
    shFile << "\n";
    shFile << "# CMake 設定 (Emscripten)\n";
    shFile << "emcmake cmake ..\n";
    shFile << "\n";
    shFile << "# ビルド\n";
    shFile << "cmake --build .\n";
    shFile << "\n";
    shFile << "echo \"\"\n";
    shFile << "echo \"Build complete! Output files are in bin/\"\n";
    shFile << "echo \"To test locally:\"\n";
    shFile << "echo \"  cd ../bin && python3 -m http.server 8080\"\n";
    shFile << "echo \"  Open http://localhost:8080/$(basename $(pwd)).html\"\n";
    shFile.close();

    // 実行権限を付与
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
    batFile << "REM ビルドディレクトリ作成\r\n";
    batFile << "if not exist build-web mkdir build-web\r\n";
    batFile << "cd build-web\r\n";
    batFile << "\r\n";
    batFile << "REM CMake 設定 (Emscripten)\r\n";
    batFile << "call emcmake cmake ..\r\n";
    batFile << "if errorlevel 1 goto error\r\n";
    batFile << "\r\n";
    batFile << "REM ビルド\r\n";
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
