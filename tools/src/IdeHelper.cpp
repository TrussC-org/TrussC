#include "IdeHelper.h"
#include "ProjectName.h"
#include "GeneratedFile.h"
#include "ProcessCapture.h"
#include <fstream>
#include <sstream>

using namespace std;

string IdeHelper::openInIde(IdeType ideType, const string& projectPath) {
    const string path = fs::absolute(projectPath).string();
    vector<string> argv;
    projectOps::ProcessOptions options;
    options.workingDirectory = path;
    options.inheritOutput = true;

#ifdef __APPLE__
    switch (ideType) {
        case IdeType::VSCode:
            argv = {"open", "-a", "Visual Studio Code", path};
            break;
        case IdeType::Cursor:
            argv = {"open", "-a", "Cursor", path};
            break;
        case IdeType::Xcode: {
            // Find and open xcode/*.xcodeproj
            string xcodePath = path + "/xcode";
            if (fs::exists(xcodePath)) {
                for (const auto& entry : fs::directory_iterator(xcodePath)) {
                    string name = entry.path().filename().string();
                    if (name.find(".xcodeproj") != string::npos) {
                        argv = {"open", entry.path().string()};
                        break;
                    }
                }
            }
            if (argv.empty()) {
                return "Xcode project not found. Run Update first.";
            }
            break;
        }
        case IdeType::VisualStudio:
            return "Visual Studio is not available on macOS";
        case IdeType::CMakeOnly:
            argv = {"open", "-a", "Terminal", path};
            break;
    }
#elif defined(_WIN32)
    options.waitForExit = false;
    switch (ideType) {
        case IdeType::VSCode:
        case IdeType::Cursor:
            options.hideConsole = true;
            projectOps::runProcess({(ideType == IdeType::VSCode) ? "code" : "cursor", path}, {}, options);
            return "";
        case IdeType::Xcode:
            return "Xcode is not available on Windows";
        case IdeType::VisualStudio: {
            // Open the solution with its registered application.
            string vsPath = path + "/vs";
            if (fs::exists(vsPath)) {
                for (const auto& entry : fs::directory_iterator(vsPath)) {
                    string name = entry.path().filename().string();
                    if (name.find(".sln") != string::npos) {
                        projectOps::runProcess({"explorer.exe", entry.path().string()}, {}, options);
                        return "";
                    }
                }
            }
            return "Visual Studio project not found. Run Update first.";
        }
        case IdeType::CMakeOnly:
            // Start an interactive terminal in the project directory.
            argv = {"cmd.exe", "/k"};
            options.newConsole = true;
            break;
    }
#else
    // Linux
    switch (ideType) {
        case IdeType::VSCode:
            argv = {"code", path};
            break;
        case IdeType::Cursor:
            argv = {"cursor", path};
            break;
        case IdeType::Xcode:
            return "Xcode is not available on Linux";
        case IdeType::VisualStudio:
            return "Visual Studio is not available on Linux";
        case IdeType::CMakeOnly:
            if (projectOps::runProcess({"x-terminal-emulator", "--working-directory=" + path}, {}, options).exitCode == 0)
                return "";
            argv = {"gnome-terminal", "--working-directory=" + path};
            break;
    }
#endif

    if (!argv.empty() && projectOps::runProcess(argv, {}, options).exitCode != 0) {
        return "Failed to open project in " + string(getIdeName(ideType));
    }

    return "";  // Success
}

void IdeHelper::generateXcodeSchemes(const string& path) {
    string xcodePath = path + "/xcode";
    string projectName = sanitizeProjectName(fs::path(path).filename().string());

    // Find .xcodeproj
    string xcodeprojPath;
    if (!fs::exists(xcodePath)) return;

    for (const auto& entry : fs::directory_iterator(xcodePath)) {
        if (entry.path().extension() == ".xcodeproj") {
            xcodeprojPath = entry.path().string();
            break;
        }
    }
    if (xcodeprojPath.empty()) return;

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

    // Generate Debug scheme (keep default buildConfiguration = "Debug")
    string debugScheme = schemesDir + "/" + projectName + " Debug.xcscheme";
    writeGeneratedFile(debugScheme, schemeContent);

    // Generate Release scheme (change buildConfiguration to RelWithDebInfo)
    string releaseContent = schemeContent;
    string searchStr = "buildConfiguration=\"Debug\"";
    string replaceStr = "buildConfiguration=\"RelWithDebInfo\"";
    size_t pos = 0;
    while ((pos = releaseContent.find(searchStr, pos)) != string::npos) {
        releaseContent.replace(pos, searchStr.length(), replaceStr);
        pos += replaceStr.length();
    }
    string releaseScheme = schemesDir + "/" + projectName + " Release.xcscheme";
    writeGeneratedFile(releaseScheme, releaseContent);

    // Keep the original until both replacement schemes are safely written.
    if (originalScheme != debugScheme && originalScheme != releaseScheme) {
        fs::remove(originalScheme);
    }
}

const char* IdeHelper::getIdeName(IdeType type) {
    switch (type) {
        case IdeType::CMakeOnly: return "CMake only";
        case IdeType::VSCode: return "VSCode";
        case IdeType::Cursor: return "Cursor";
        case IdeType::Xcode: return "Xcode";
        case IdeType::VisualStudio: return "Visual Studio";
    }
    return "Unknown";
}

const char* IdeHelper::getIdeId(IdeType type) {
    switch (type) {
        case IdeType::CMakeOnly: return "cmake";
        case IdeType::VSCode: return "vscode";
        case IdeType::Cursor: return "cursor";
        case IdeType::Xcode: return "xcode";
        case IdeType::VisualStudio: return "vs";
    }
    return "vscode";
}

bool IdeHelper::parseIdeId(const string& id, IdeType& out) {
    if      (id == "vscode") out = IdeType::VSCode;
    else if (id == "cursor") out = IdeType::Cursor;
    else if (id == "xcode")  out = IdeType::Xcode;
    else if (id == "vs")     out = IdeType::VisualStudio;
    else if (id == "cmake")  out = IdeType::CMakeOnly;
    else return false;
    return true;
}
