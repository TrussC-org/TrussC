#include "TrussC.h"
#include "tcApp.h"
#include "ProjectGenerator.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstdlib>

using namespace std;
namespace fs = std::filesystem;

// Helper to scan available addons
static void scanAddons(const string& tcRoot, vector<string>& addons) {
    addons.clear();
    if (tcRoot.empty()) return;

    string addonsPath = tcRoot + "/addons";
    if (!fs::exists(addonsPath)) return;

    for (const auto& entry : fs::directory_iterator(addonsPath)) {
        if (entry.is_directory()) {
            string name = entry.path().filename().string();
            if (name.substr(0, 3) == "tcx") {
                addons.push_back(name);
            }
        }
    }
    sort(addons.begin(), addons.end());
}

// Helper to parse existing addons.make
static void parseAddonsMake(const string& projectPath, const vector<string>& availableAddons, vector<int>& addonSelected) {
    addonSelected.assign(availableAddons.size(), 0);

    string addonsMakePath = projectPath + "/addons.make";
    if (!fs::exists(addonsMakePath)) return;

    ifstream addonsFile(addonsMakePath);
    string line;
    while (getline(addonsFile, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        string addonName = line.substr(start, end - start + 1);

        if (addonName.empty() || addonName[0] == '#') continue;

        for (size_t i = 0; i < availableAddons.size(); i++) {
            if (availableAddons[i] == addonName) {
                addonSelected[i] = 1;
                break;
            }
        }
    }
}

// Helper to try auto-detect TC_ROOT
static string autoDetectTcRoot() {
    // 1. Try environment variable
    const char* envRoot = std::getenv("TRUSSC_DIR");
    if (envRoot && fs::exists(string(envRoot) + "/trussc/CMakeLists.txt")) {
        return string(envRoot);
    }

    // 2. Try relative path from executable
    // Typically projectGenerator is in TRUSSC_ROOT/projectGenerator/projectGenerator.app/Contents/MacOS/
    // or TRUSSC_ROOT/projectGenerator/tools/projectGenerator/bin/
    // We assume we are in TRUSSC_ROOT/... something
    
    // Search up to 5 parent directories
    fs::path exePath = platform::getExecutablePath();
    fs::path searchPath = exePath.parent_path();

    #ifdef __APPLE__
    // Go up 3 levels: MacOS -> Contents -> .app -> parent
    for (int i = 0; i < 3 && searchPath.has_parent_path(); i++) {
        searchPath = searchPath.parent_path();
    }
    #endif

    for (int i = 0; i < 5 && searchPath.has_parent_path(); i++) {
        fs::path checkPath = searchPath / "trussc" / "CMakeLists.txt";
        if (fs::exists(checkPath)) {
            return searchPath.string();
        }
        searchPath = searchPath.parent_path();
    }

    return "";
}

void printHelp() {
    cout << "Usage: projectGenerator [options]" << endl;
    cout << "Options:" << endl;
    cout << "  --update <path>          Update existing project (path to project folder)" << endl;
    cout << "  --generate               Generate new project (requires --name and --dir)" << endl;
    cout << "  --name <name>            Project name (for --generate)" << endl;
    cout << "  --dir <path>             Project parent directory (for --generate)" << endl;
    cout << "  --tc-root <path>         Path to TrussC root directory" << endl;
    cout << "  --web                    Enable Web build (Emscripten)" << endl;
    cout << "  --ide <type>             IDE type (vscode, cursor, xcode, vs, cmake)" << endl;
    cout << "  --help                   Show this help" << endl;
}

int main(int argc, char* argv[]) {
    vector<string> args(argv + 1, argv + argc);

    bool cliMode = false;
    bool updateMode = false;
    bool generateMode = false;
    string targetPath; // For update: project folder. For generate: project PARENT folder (from --dir)
    string projectName;
    string tcRoot;
    bool web = false;
    string ideStr = "vscode";

    // Parse arguments
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--update" && i + 1 < args.size()) {
            cliMode = true;
            updateMode = true;
            targetPath = args[++i];
        } else if (args[i] == "--generate") {
            cliMode = true;
            generateMode = true;
        } else if (args[i] == "--name" && i + 1 < args.size()) {
            projectName = args[++i];
        } else if (args[i] == "--dir" && i + 1 < args.size()) {
            targetPath = args[++i]; // reusing targetPath
        } else if (args[i] == "--tc-root" && i + 1 < args.size()) {
            tcRoot = args[++i];
        } else if (args[i] == "--web") {
            web = true;
        } else if (args[i] == "--ide" && i + 1 < args.size()) {
            ideStr = args[++i];
        } else if (args[i] == "--help") {
            printHelp();
            return 0;
        }
    }

    if (cliMode) {
        // Detect TC_ROOT
        if (tcRoot.empty()) {
            tcRoot = autoDetectTcRoot();
        }
        if (tcRoot.empty()) {
            cerr << "Error: Could not detect TrussC root. Please use --tc-root <path> or set TRUSSC_DIR env var." << endl;
            return 1;
        }
        
        // Scan addons
        vector<string> availableAddons;
        scanAddons(tcRoot, availableAddons);
        
        ProjectSettings settings;
        settings.tcRoot = tcRoot;
        settings.generateWebBuild = web;
        
        // Parse IDE type
        if (ideStr == "vscode") settings.ideType = IdeType::VSCode;
        else if (ideStr == "cursor") settings.ideType = IdeType::Cursor;
        else if (ideStr == "xcode") settings.ideType = IdeType::Xcode;
        else if (ideStr == "vs") settings.ideType = IdeType::VisualStudio;
        else if (ideStr == "cmake") settings.ideType = IdeType::CMakeOnly;
        else {
            cerr << "Warning: Unknown IDE type '" << ideStr << "', using VSCode." << endl;
            settings.ideType = IdeType::VSCode;
        }
        
        // Set template path
        settings.templatePath = tcRoot + "/examples/templates/emptyExample";
        if (!fs::exists(settings.templatePath)) {
            cerr << "Error: Template not found at " << settings.templatePath << endl;
            return 1;
        }

        if (updateMode) {
            if (!fs::is_directory(targetPath)) {
                cerr << "Error: Project path '" << targetPath << "' does not exist." << endl;
                return 1;
            }
            
            // Parse existing addons.make
            parseAddonsMake(targetPath, availableAddons, settings.addonSelected);
            settings.addons = availableAddons; // Pass all available, selection is in addonSelected
            
            // Re-create generator with updated settings (addons)
            ProjectGenerator updateGen(settings);
            updateGen.setLogCallback([](const string& msg) { cout << msg << endl; });
            
            string result = updateGen.update(targetPath);
            if (!result.empty()) {
                cerr << "Update failed: " << result << endl;
                return 1;
            }
            cout << "Project updated successfully: " << targetPath << endl;
            
        } else if (generateMode) {
            if (projectName.empty() || targetPath.empty()) {
                cerr << "Error: --name and --dir are required for --generate" << endl;
                return 1;
            }
            
            settings.projectName = projectName;
            settings.projectDir = targetPath;
            settings.addons = availableAddons;
            settings.addonSelected.assign(availableAddons.size(), 0); // No addons by default for CLI generate
            
            ProjectGenerator genGen(settings);
            genGen.setLogCallback([](const string& msg) { cout << msg << endl; });
            
            string result = genGen.generate();
            if (!result.empty()) {
                cerr << "Generation failed: " << result << endl;
                return 1;
            }
            cout << "Project generated successfully: " << targetPath << "/" << projectName << endl;
        }
        
        return 0;
    }

    WindowSettings settings;
    settings.title = "TrussC Project Generator";
    settings.width = 500;
    settings.height = 520;
    return runApp<tcApp>(settings);
}
