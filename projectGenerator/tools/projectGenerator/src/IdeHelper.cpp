#include "IdeHelper.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace std;

string IdeHelper::openInIde(IdeType ideType, const string& path) {
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
            // Find and open xcode/*.xcodeproj
            string xcodePath = path + "/xcode";
            if (fs::exists(xcodePath)) {
                for (const auto& entry : fs::directory_iterator(xcodePath)) {
                    string name = entry.path().filename().string();
                    if (name.find(".xcodeproj") != string::npos) {
                        cmd = "open \"" + entry.path().string() + "\"";
                        break;
                    }
                }
            }
            if (cmd.empty()) {
                return "Xcode project not found. Run Update first.";
            }
            break;
        }
        case IdeType::VisualStudio:
            return "Visual Studio is not available on macOS";
        case IdeType::CMakeOnly:
            cmd = "open -a Terminal \"" + path + "\"";
            break;
    }
#elif defined(_WIN32)
    // Windows: コンソールウィンドウを出さずに起動
    switch (ideType) {
        case IdeType::VSCode:
        case IdeType::Cursor: {
            // code / cursor コマンドをコンソール非表示で実行
            string exe = (ideType == IdeType::VSCode) ? "code" : "cursor";
            string fullCmd = "cmd.exe /c " + exe + " \"" + path + "\"";
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            if (CreateProcessA(nullptr, const_cast<char*>(fullCmd.c_str()),
                               nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            return "";
        }
        case IdeType::Xcode:
            return "Xcode is not available on Windows";
        case IdeType::VisualStudio: {
            // .slnファイルをShellExecuteで直接開く（コンソール不要）
            string vsPath = path + "/vs";
            if (fs::exists(vsPath)) {
                for (const auto& entry : fs::directory_iterator(vsPath)) {
                    string name = entry.path().filename().string();
                    if (name.find(".sln") != string::npos) {
                        ShellExecuteA(nullptr, "open", entry.path().string().c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
                        return "";
                    }
                }
            }
            return "Visual Studio project not found. Run Update first.";
        }
        case IdeType::CMakeOnly:
            // ターミナルを意図的に表示する
            cmd = "start cmd /k \"cd /d " + path + "\"";
            break;
    }
#else
    // Linux
    switch (ideType) {
        case IdeType::VSCode:
            cmd = "code \"" + path + "\"";
            break;
        case IdeType::Cursor:
            cmd = "cursor \"" + path + "\"";
            break;
        case IdeType::Xcode:
            return "Xcode is not available on Linux";
        case IdeType::VisualStudio:
            return "Visual Studio is not available on Linux";
        case IdeType::CMakeOnly:
            cmd = "x-terminal-emulator --working-directory=\"" + path + "\" || gnome-terminal --working-directory=\"" + path + "\"";
            break;
    }
#endif

    if (!cmd.empty()) {
        system(cmd.c_str());
    }

    return "";  // Success
}

void IdeHelper::generateXcodeSchemes(const string& path) {
    string xcodePath = path + "/xcode";
    string projectName = fs::path(path).filename().string();

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

    // Delete original scheme
    fs::remove(originalScheme);

    // Generate Debug scheme (keep default buildConfiguration = "Debug")
    string debugScheme = schemesDir + "/" + projectName + " Debug.xcscheme";
    ofstream debugFile(debugScheme);
    debugFile << schemeContent;
    debugFile.close();

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
    ofstream releaseFile(releaseScheme);
    releaseFile << releaseContent;
    releaseFile.close();
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
