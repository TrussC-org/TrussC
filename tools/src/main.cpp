#include "TrussC.h"
#include "tcApp.h"
#include "ProjectGenerator.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#endif

using namespace std;
using namespace tc;
namespace fs = std::filesystem;

// =============================================================================
// Helpers
// =============================================================================

static void scanAddons(const string& tcRoot, vector<string>& addons) {
    addons.clear();
    if (tcRoot.empty()) return;
    string addonsPath = tcRoot + "/addons";
    if (!fs::exists(addonsPath)) return;
    for (const auto& entry : fs::directory_iterator(addonsPath)) {
        if (entry.is_directory()) {
            string name = entry.path().filename().string();
            if (name.substr(0, 3) == "tcx") addons.push_back(name);
        }
    }
    sort(addons.begin(), addons.end());
}

static void parseAddonsMake(const string& projectPath,
                            const vector<string>& availableAddons,
                            vector<int>& addonSelected) {
    addonSelected.assign(availableAddons.size(), 0);
    string addonsMakePath = projectPath + "/addons.make";
    if (!fs::exists(addonsMakePath)) return;

    ifstream addonsFile(addonsMakePath);
    string line;
    while (getline(addonsFile, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        string name = line.substr(start, end - start + 1);
        if (name.empty() || name[0] == '#') continue;
        for (size_t i = 0; i < availableAddons.size(); ++i) {
            if (availableAddons[i] == name) { addonSelected[i] = 1; break; }
        }
    }
}

static string autoDetectTcRoot() {
    const char* envRoot = std::getenv("TRUSSC_DIR");
    if (envRoot && fs::exists(string(envRoot) + "/core/cmake/trussc_app.cmake")) {
        return string(envRoot);
    }

    // Walk up from the executable looking for core/cmake/trussc_app.cmake.
    // Typically trusscli lives in TRUSSC_ROOT/tools/bin/trusscli.app/Contents/MacOS/
    // (Linux/Windows: TRUSSC_ROOT/tools/bin/trusscli[.exe])
    // Use canonical() to resolve symlinks — when invoked via /usr/local/bin/trusscli,
    // the raw path would be the symlink location, not the actual binary.
    fs::path searchPath = fs::canonical(getExecutablePath()).parent_path();
    #ifdef __APPLE__
    // Climb out of MacOS -> Contents -> .app -> parent dir
    for (int i = 0; i < 3 && searchPath.has_parent_path(); ++i) {
        searchPath = searchPath.parent_path();
    }
    #endif
    for (int i = 0; i < 5 && searchPath.has_parent_path(); ++i) {
        if (fs::exists(searchPath / "core" / "cmake" / "trussc_app.cmake")) {
            return searchPath.string();
        }
        searchPath = searchPath.parent_path();
    }
    return "";
}

// Walk up from `startPath` (or CWD if empty) looking for a TrussC project
// marker (CMakeLists.txt + addons.make in the same dir). Returns absolute
// path to the project root, or empty string if none found.
static string autoDetectProjectRoot(const string& startPath) {
    fs::path searchPath = fs::absolute(
        startPath.empty() ? fs::current_path() : fs::path(startPath));
    for (int i = 0; i < 10; ++i) {
        if (fs::exists(searchPath / "CMakeLists.txt") &&
            fs::exists(searchPath / "addons.make")) {
            return searchPath.string();
        }
        if (!searchPath.has_parent_path() ||
            searchPath == searchPath.parent_path()) break;
        searchPath = searchPath.parent_path();
    }
    return "";
}

// Map IDE name string to enum. Returns true on success.
static bool parseIdeType(const string& s, IdeType& out) {
    if      (s == "vscode") out = IdeType::VSCode;
    else if (s == "cursor") out = IdeType::Cursor;
    else if (s == "xcode")  out = IdeType::Xcode;
    else if (s == "vs")     out = IdeType::VisualStudio;
    else if (s == "cmake")  out = IdeType::CMakeOnly;
    else return false;
    return true;
}

// Parse a -a / --addon / --addons value: accepts a single name or a comma-list.
// Catches the common "stray space after comma" mistake (which the shell has
// already split into two argv elements by the time we see it: the previous
// arg ends with ',' and the next arg looks like a bare name).
static bool parseAddonValue(const string& value,
                            vector<string>& out,
                            string& errMsg) {
    if (value.empty()) {
        errMsg = "addon flag requires a value";
        return false;
    }
    if (value.back() == ',') {
        errMsg = "trailing comma in addon value '" + value +
                 "' — looks like a stray space after a comma. "
                 "Write 'tcxOsc,tcxIME' (no space), or use repeat form '-a tcxOsc -a tcxIME'.";
        return false;
    }
    string current;
    for (char c : value) {
        if (c == ',') {
            if (current.empty()) {
                errMsg = "empty element in addon list '" + value + "'";
                return false;
            }
            out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return true;
}

// =============================================================================
// Subprocess: run an external command with inherited stdout/stderr
// =============================================================================

// Run a command with argv passed as an array (no shell involvement). stdout and
// stderr are inherited from the parent process so build output streams through.
// Returns the child's exit code, or -1 on spawn failure.
static int runProcess(const vector<string>& argv) {
    if (argv.empty()) return -1;
#ifdef _WIN32
    // Build a null-terminated argv for _spawnvp
    vector<const char*> cargv;
    for (const auto& a : argv) cargv.push_back(a.c_str());
    cargv.push_back(nullptr);
    return (int)_spawnvp(_P_WAIT, cargv[0], cargv.data());
#else
    // POSIX: posix_spawnp + waitpid
    vector<char*> cargv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    extern char** environ;
    pid_t pid = 0;
    int err = posix_spawnp(&pid, cargv[0], nullptr, nullptr, cargv.data(), environ);
    if (err != 0) {
        cerr << "Error: failed to spawn '" << argv[0] << "': " << strerror(err) << "\n";
        return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// =============================================================================
// Doctor: environment checks
// =============================================================================

struct CaptureResult { int exitCode; string output; };

// Windows uses _popen/_pclose (POSIX names without underscore are not available)
#ifdef _WIN32
#define tc_popen  _popen
#define tc_pclose _pclose
#else
#define tc_popen  popen
#define tc_pclose pclose
#endif

static CaptureResult captureCommand(const string& cmd) {
    string output;
    FILE* pipe = tc_popen(cmd.c_str(), "r");
    if (!pipe) return {-1, ""};
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int status = tc_pclose(pipe);
#ifdef _WIN32
    return {status, output};
#else
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
#endif
}

enum class CheckStatus { OK, Warning, Error, Skipped };

struct CheckResult {
    string name;
    CheckStatus status = CheckStatus::OK;
    string detail;
    string hint;
    bool essential = false;
};

static CheckResult checkCMake() {
    CheckResult r{"CMake", CheckStatus::OK, "", "", true};
    auto [code, out] = captureCommand("cmake --version 2>/dev/null");
    if (code != 0 || out.empty()) {
        r.status = CheckStatus::Error;
        r.detail = "not found";
#ifdef __APPLE__
        r.hint = "brew install cmake";
#elif defined(__linux__)
        r.hint = "sudo apt install cmake  (or: sudo dnf install cmake)";
#else
        r.hint = "https://cmake.org/download/";
#endif
        return r;
    }
    // Parse "cmake version X.Y.Z"
    size_t pos = out.find("cmake version ");
    if (pos != string::npos) {
        size_t end = out.find('\n', pos);
        r.detail = out.substr(pos + 14, end - pos - 14);
    } else {
        r.detail = "(version unknown)";
    }
    return r;
}

static CheckResult checkCompiler() {
    CheckResult r{"C++ Compiler", CheckStatus::OK, "", "", true};
#ifdef __APPLE__
    auto [code, out] = captureCommand("clang++ --version 2>/dev/null");
    if (code != 0 || out.empty()) {
        r.status = CheckStatus::Error;
        r.detail = "not found";
        r.hint = "xcode-select --install";
        return r;
    }
    size_t pos = out.find("Apple clang version ");
    if (pos != string::npos) {
        size_t end = out.find(' ', pos + 20);
        r.detail = "Apple Clang " + out.substr(pos + 20, end - pos - 20) + " (C++20)";
    } else {
        r.detail = "(detected)";
    }
#elif defined(__linux__)
    auto [code, out] = captureCommand("g++ --version 2>/dev/null");
    if (code != 0 || out.empty()) {
        auto [code2, out2] = captureCommand("clang++ --version 2>/dev/null");
        if (code2 != 0 || out2.empty()) {
            r.status = CheckStatus::Error;
            r.detail = "not found (need g++ or clang++)";
            r.hint = "sudo apt install g++  (or: sudo apt install clang)";
            return r;
        }
        r.detail = "(clang++)";
        return r;
    }
    size_t pos = out.find(')');
    if (pos != string::npos && pos + 2 < out.size()) {
        size_t end = out.find('\n', pos);
        r.detail = "g++ " + out.substr(pos + 2, end - pos - 2);
    } else {
        r.detail = "(g++)";
    }
#elif defined(_WIN32)
    auto [code, out] = captureCommand("cl 2>&1");
    r.detail = code == 0 ? "(MSVC)" : "not found";
    if (code != 0) {
        r.status = CheckStatus::Error;
        r.hint = "Install Visual Studio with C++ workload";
    }
#endif
    return r;
}

static CheckResult checkTrussCCore(const string& tcRoot) {
    CheckResult r{"TrussC core", CheckStatus::OK, "found", "", true};
    if (tcRoot.empty() ||
        !fs::exists(tcRoot + "/core/cmake/trussc_app.cmake")) {
        r.status = CheckStatus::Error;
        r.detail = "not found";
        r.hint = "Ensure you're inside a TrussC repo or set TRUSSC_DIR";
    }
    return r;
}

static CheckResult checkPlatformSDK() {
#ifdef __APPLE__
    CheckResult r{"macOS SDK", CheckStatus::OK, "", "", true};
    auto [code, out] = captureCommand("xcrun --show-sdk-version 2>/dev/null");
    if (code != 0 || out.empty()) {
        r.status = CheckStatus::Error;
        r.detail = "not found";
        r.hint = "xcode-select --install";
    } else {
        size_t end = out.find('\n');
        r.detail = out.substr(0, end);
    }
    return r;
#elif defined(__linux__)
    CheckResult r{"Linux build deps", CheckStatus::OK, "", "", true};
    auto [code, out] = captureCommand("pkg-config --exists gl egl 2>/dev/null; echo $?");
    // Simple heuristic: check for development headers
    bool hasGL = fs::exists("/usr/include/GL/gl.h") ||
                 fs::exists("/usr/include/GLES2/gl2.h");
    bool hasEGL = fs::exists("/usr/include/EGL/egl.h");
    if (!hasGL || !hasEGL) {
        r.status = CheckStatus::Error;
        r.detail = "missing OpenGL/EGL development headers";
        r.hint = "sudo apt install libgl1-mesa-dev libegl1-mesa-dev";
    } else {
        r.detail = "GL + EGL headers found";
    }
    return r;
#elif defined(_WIN32)
    CheckResult r{"Windows SDK", CheckStatus::OK, "(detected via MSVC)", "", true};
    return r;
#else
    return {"Platform SDK", CheckStatus::Skipped, "", "", false};
#endif
}

static CheckResult checkEmscripten() {
    CheckResult r{"Emscripten", CheckStatus::OK, "", ""};
    auto [code, out] = captureCommand("emcc --version 2>/dev/null");
    if (code != 0 || out.empty()) {
        r.status = CheckStatus::Error;
        r.detail = "not found";
        r.hint = "https://emscripten.org/docs/getting_started/downloads.html";
    } else {
        size_t pos = out.find("emcc");
        size_t end = out.find('\n', pos);
        r.detail = (pos != string::npos) ? out.substr(pos, end - pos) : "(detected)";
    }
    return r;
}

static CheckResult checkAndroidNDK() {
    CheckResult r{"Android NDK", CheckStatus::OK, "", ""};
    const char* home = std::getenv("ANDROID_HOME");
    if (!home || !fs::exists(string(home))) {
        r.status = CheckStatus::Error;
        r.detail = "ANDROID_HOME not set";
        r.hint = "Set ANDROID_HOME to your Android SDK path";
    } else if (!fs::exists(string(home) + "/ndk")) {
        r.status = CheckStatus::Error;
        r.detail = "NDK directory not found in ANDROID_HOME";
        r.hint = "Install NDK via Android Studio SDK Manager";
    } else {
        r.detail = string(home);
    }
    return r;
}

static CheckResult checkNinja() {
    CheckResult r{"Ninja", CheckStatus::OK, "", ""};
    auto [code, out] = captureCommand("ninja --version 2>/dev/null");
    if (code != 0 || out.empty()) {
        r.status = CheckStatus::Warning;
        r.detail = "not found (optional — cmake uses make as fallback)";
#ifdef __APPLE__
        r.hint = "brew install ninja";
#elif defined(__linux__)
        r.hint = "sudo apt install ninja-build";
#endif
    } else {
        size_t end = out.find('\n');
        r.detail = out.substr(0, end);
    }
    return r;
}

static CheckResult checkGit() {
    CheckResult r{"Git", CheckStatus::OK, "", ""};
    auto [code, out] = captureCommand("git --version 2>/dev/null");
    if (code != 0 || out.empty()) {
        r.status = CheckStatus::Warning;
        r.detail = "not found";
    } else {
        size_t pos = out.find("git version ");
        size_t end = out.find('\n', pos);
        r.detail = (pos != string::npos) ? out.substr(pos + 12, end - pos - 12) : "(detected)";
    }
    return r;
}

// =============================================================================
// Common helpers for project-based subcommands
// =============================================================================

// Resolve project path + TC_ROOT for commands that operate on an existing
// project (update / add / remove / info / build / run). Prints an error and
// returns non-zero on failure. Both out-params are populated on success.
static int resolveProjectAndTcRoot(const string& explicitPath,
                                   const string& explicitTcRoot,
                                   string& outProjectPath,
                                   string& outTcRoot) {
    if (explicitPath.empty()) {
        outProjectPath = autoDetectProjectRoot("");
        if (outProjectPath.empty()) {
            cerr << "Error: not inside a TrussC project (no CMakeLists.txt + "
                 << "addons.make found in CWD or any parent).\n"
                 << "Use '-p <path>' or run from inside a project.\n";
            return 1;
        }
    } else if (!fs::is_directory(explicitPath)) {
        cerr << "Error: project path '" << explicitPath << "' does not exist\n";
        return 1;
    } else {
        outProjectPath = explicitPath;
    }

    outTcRoot = explicitTcRoot.empty() ? autoDetectTcRoot() : explicitTcRoot;
    if (outTcRoot.empty()) {
        cerr << "Error: could not detect TrussC root. "
             << "Use --tc-root <path> or set TRUSSC_DIR.\n";
        return 1;
    }
    return 0;
}

// Rebuild a project from a populated ProjectSettings. Used by update / add /
// remove to regenerate CMakeLists.txt / CMakePresets.json after any change.
static int runProjectUpdate(ProjectSettings& settings, const string& projectPath) {
    ProjectGenerator gen(settings);
    gen.setLogCallback([](const string& msg) { cout << msg << endl; });
    string err = gen.update(projectPath);
    if (!err.empty()) {
        cerr << "Error: " << err << "\n";
        return 1;
    }
    return 0;
}

// Write an addons.make file containing the selected addon names. Called from
// add / remove after modifying the selection set.
static bool writeAddonsMake(const string& projectPath,
                            const vector<string>& selectedAddons) {
    string path = projectPath + "/addons.make";
    ofstream out(path);
    if (!out) {
        cerr << "Error: could not write " << path << "\n";
        return false;
    }
    out << "# TrussC addons - one addon per line\n";
    for (const string& name : selectedAddons) {
        out << name << "\n";
    }
    return true;
}

// Forward declarations for functions defined later
static string findCMake();
static vector<string> readProjectAddons(const string& projectPath);

// =============================================================================
// Subcommand: new
// =============================================================================

static void printNewHelp() {
    cout << "Usage: trusscli new <path> [options]\n"
         << "\n"
         << "Create a new TrussC project at <path>. The project name is the last\n"
         << "path component (e.g. `trusscli new ./apps/myApp` creates ./apps/myApp\n"
         << "with name 'myApp').\n"
         << "\n"
         << "Options:\n"
         << "      --web                  Enable Web (WebAssembly) build\n"
         << "      --android              Enable Android build\n"
         << "      --ios                  Enable iOS build\n"
         << "      --ide <type>           IDE: vscode, cursor, xcode, vs, cmake (default: vscode)\n"
         << "  -a, --addon <name>         Add an addon. Repeatable, comma-list also OK\n"
         << "      --addons <list>        Same as -a, alternative spelling\n"
         << "      --tc-root <path>       Path to TrussC root directory (auto-detected by default)\n"
         << "  -h, --help                 Show this help\n";
}

static int cmdNew(const vector<string>& args) {
    string positional;
    bool web = false, android = false, ios = false;
    string ideStr = "vscode";
    string tcRoot;
    vector<string> requestedAddons;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printNewHelp(); return 0; }
        else if (a == "--web") web = true;
        else if (a == "--android") android = true;
        else if (a == "--ios") ios = true;
        else if (a == "--ide") {
            if (!needValue(i, a, ideStr)) return 1;
        }
        else if (a == "-a" || a == "--addon" || a == "--addons") {
            string val;
            if (!needValue(i, a, val)) return 1;
            string err;
            if (!parseAddonValue(val, requestedAddons, err)) {
                cerr << "Error: " << err << "\n";
                return 1;
            }
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n";
            cerr << "Run 'trusscli new --help' for usage.\n";
            return 1;
        }
        else {
            if (!positional.empty()) {
                cerr << "Error: 'new' takes a single path argument "
                     << "(got '" << positional << "' and '" << a << "')\n";
                return 1;
            }
            positional = a;
        }
    }

    if (positional.empty()) {
        cerr << "Error: 'new' requires a project path\n";
        cerr << "Usage: trusscli new <path> [options]\n";
        return 1;
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root. "
             << "Use --tc-root <path> or set TRUSSC_DIR.\n";
        return 1;
    }

    // Derive name + parent dir from the positional path
    fs::path projPath = fs::absolute(positional);
    string projectName = projPath.filename().string();
    string projectDir = projPath.parent_path().string();
    if (projectName.empty()) {
        cerr << "Error: could not derive project name from path '" << positional << "'\n";
        return 1;
    }
    if (fs::exists(projPath)) {
        cerr << "Error: '" << projPath.string() << "' already exists\n";
        return 1;
    }

    // Scan available addons and resolve the requested ones
    vector<string> availableAddons;
    scanAddons(tcRoot, availableAddons);
    vector<int> addonSelected(availableAddons.size(), 0);
    for (const string& want : requestedAddons) {
        bool found = false;
        for (size_t i = 0; i < availableAddons.size(); ++i) {
            if (availableAddons[i] == want) {
                addonSelected[i] = 1;
                found = true;
                break;
            }
        }
        if (!found) {
            cerr << "Error: addon '" << want << "' not found in " << tcRoot << "/addons/\n";
            cerr << "Available addons:\n";
            for (const string& aa : availableAddons) cerr << "  " << aa << "\n";
            return 1;
        }
    }

    ProjectSettings settings;
    settings.tcRoot = tcRoot;
    settings.projectName = projectName;
    settings.projectDir = projectDir;
    settings.addons = availableAddons;
    settings.addonSelected = addonSelected;
    settings.generateWebBuild = web;
    settings.generateAndroidBuild = android;
    settings.generateIosBuild = ios;
    settings.detectBuildEnvironment();

    if (!parseIdeType(ideStr, settings.ideType)) {
        cerr << "Error: unknown IDE type '" << ideStr
             << "'. Valid: vscode, cursor, xcode, vs, cmake\n";
        return 1;
    }

    settings.templatePath = tcRoot + "/examples/templates/emptyExample";
    if (!fs::exists(settings.templatePath)) {
        cerr << "Error: template not found at " << settings.templatePath << "\n";
        return 1;
    }

    ProjectGenerator gen(settings);
    gen.setLogCallback([](const string& msg) { cout << msg << endl; });
    string err = gen.generate();
    if (!err.empty()) {
        cerr << "Error: " << err << "\n";
        return 1;
    }
    cout << "Project created: " << projPath.string() << "\n";
    return 0;
}

// =============================================================================
// Subcommand: cp (duplicate an existing project)
// =============================================================================

static void printCpHelp() {
    cout << "Usage: trusscli cp <src> <dst> [options]\n"
         << "\n"
         << "Copy an existing TrussC project to a new location. If <src> is a\n"
         << "git work tree, only git-tracked and non-ignored files are copied\n"
         << "(so .gitignore controls what counts as generated). Otherwise a\n"
         << "built-in skip list for known build/IDE artifacts is used.\n"
         << "Generated build / IDE files are re-created at the destination.\n"
         << "\n"
         << "Options:\n"
         << "  -n, --dry-run              Show what would be copied, don't modify anything\n"
         << "      --no-git               Force the built-in skip list even if <src> is a git repo\n"
         << "      --web                  Enable Web (WebAssembly) build\n"
         << "      --android              Enable Android build\n"
         << "      --ios                  Enable iOS build\n"
         << "      --ide <type>           IDE: vscode, cursor, xcode, vs, cmake (default: vscode)\n"
         << "      --tc-root <path>       Path to TrussC root directory (auto-detected by default)\n"
         << "  -h, --help                 Show this help\n"
         << "\n"
         << "Examples:\n"
         << "  trusscli cp myApp myApp2               Duplicate ./myApp to ./myApp2\n"
         << "  trusscli cp . ../myApp2                Copy current project to sibling dir\n"
         << "  trusscli cp -n ../ref/myApp ./fork     Preview without copying\n";
}

// Returns true if the top-level entry name should be skipped when copying a
// project. Build artifacts, IDE-generated files, CMake caches, and VCS data
// are regenerated or meaningless at the destination.
// Used only when git is unavailable or --no-git is set.
static bool isSkippedInCp(const string& name) {
    // Build artifacts
    if (name == "build" || name == "bin") return true;
    if (name.rfind("build-", 0) == 0) return true;  // build-macos, build-win-msvc-x64, build-web, ...
    if (name.rfind("xcode", 0) == 0) return true;   // xcode, xcode-debug, ... (trussc pattern is 'xcode*/')
    if (name == "vs" || name == "emscripten") return true;
    // CMake generated
    if (name == "CMakePresets.json" || name == "CMakeUserPresets.json") return true;
    if (name == "CMakeCache.txt" || name == "CMakeFiles") return true;
    if (name == "compile_commands.json") return true;
    // TrussC local override (regenerated if present at build time)
    if (name == ".trussc") return true;
    // IDE / LSP caches
    if (name == ".vscode" || name == ".idea" || name == ".vs" || name == ".cache") return true;
    if (name == ".ccls-cache" || name == ".clangd") return true;
    auto endsWith = [&](const string& suffix) {
        return name.size() >= suffix.size() &&
               name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (endsWith(".xcodeproj")) return true;
    if (endsWith(".sln")) return true;
    if (endsWith(".vcxproj") || endsWith(".vcxproj.filters") || endsWith(".vcxproj.user")) return true;
    // Generator wrapper scripts
    if (name.rfind("build-web.", 0) == 0) return true;   // build-web.command / .bat / .sh
    // OS cruft
    if (name == ".DS_Store" || name == "Thumbs.db" || name == "desktop.ini") return true;
    // VCS
    if (name == ".git") return true;
    return false;
}

// Returns true if `path` sits inside a git work tree.
static bool isGitWorkTree(const fs::path& path) {
    string cmd = "git -C \"" + path.string() + "\" rev-parse --is-inside-work-tree 2>/dev/null";
    auto [code, out] = captureCommand(cmd);
    if (code != 0) return false;
    // Expect "true\n"
    return out.rfind("true", 0) == 0;
}

// Lists files under `srcRoot` as relative paths. Respects .gitignore via
// --exclude-standard, and includes both tracked and untracked (but not ignored)
// files. Caller must ensure srcRoot is a git work tree.
// Paths containing newlines would break this parser, but TrussC projects are
// not expected to have such paths (and captureCommand() reads line-by-line via
// fgets(), which can't safely handle NUL-separated output from `ls-files -z`).
static vector<string> gitListFiles(const fs::path& srcRoot) {
    // -c: cached (tracked), -o: others (untracked), --exclude-standard: honor .gitignore
    string cmd = "git -C \"" + srcRoot.string() +
                 "\" ls-files -co --exclude-standard 2>/dev/null";
    auto [code, out] = captureCommand(cmd);
    vector<string> files;
    if (code != 0) return files;
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        if (end == string::npos) end = out.size();
        string line = out.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) files.push_back(std::move(line));
        if (end == out.size()) break;
        start = end + 1;
    }
    return files;
}

static int cmdCp(const vector<string>& args) {
    string srcArg, dstArg;
    bool web = false, android = false, ios = false;
    bool dryRun = false, noGit = false;
    string ideStr = "vscode";
    string tcRoot;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printCpHelp(); return 0; }
        else if (a == "-n" || a == "--dry-run") dryRun = true;
        else if (a == "--no-git") noGit = true;
        else if (a == "--web") web = true;
        else if (a == "--android") android = true;
        else if (a == "--ios") ios = true;
        else if (a == "--ide") {
            if (!needValue(i, a, ideStr)) return 1;
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n";
            cerr << "Run 'trusscli cp --help' for usage.\n";
            return 1;
        }
        else {
            if (srcArg.empty()) srcArg = a;
            else if (dstArg.empty()) dstArg = a;
            else {
                cerr << "Error: 'cp' takes exactly two path arguments "
                     << "(got extra: '" << a << "')\n";
                return 1;
            }
        }
    }

    if (srcArg.empty() || dstArg.empty()) {
        cerr << "Error: 'cp' requires <src> and <dst>\n";
        cerr << "Usage: trusscli cp <src> <dst> [options]\n";
        return 1;
    }

    fs::path srcPath = fs::absolute(srcArg);
    if (!fs::is_directory(srcPath)) {
        cerr << "Error: source '" << srcArg << "' is not a directory\n";
        return 1;
    }
    if (!fs::exists(srcPath / "CMakeLists.txt") ||
        !fs::exists(srcPath / "addons.make")) {
        cerr << "Error: source '" << srcArg << "' is not a TrussC project "
             << "(missing CMakeLists.txt or addons.make)\n";
        return 1;
    }

    fs::path dstPath = fs::absolute(dstArg);
    if (fs::exists(dstPath)) {
        cerr << "Error: destination '" << dstPath.string() << "' already exists\n";
        return 1;
    }
    // Reject dst inside src (would loop forever if copy happens before exist check
    // is re-evaluated, and is almost certainly a typo).
    {
        fs::path d = dstPath;
        while (d.has_parent_path() && d != d.parent_path()) {
            d = d.parent_path();
            if (fs::exists(d) && fs::exists(srcPath) &&
                fs::equivalent(d, srcPath)) {
                cerr << "Error: destination '" << dstPath.string()
                     << "' is inside source '" << srcPath.string() << "'\n";
                return 1;
            }
        }
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root. "
             << "Use --tc-root <path> or set TRUSSC_DIR.\n";
        return 1;
    }

    // Inherit addon list from source
    vector<string> requestedAddons = readProjectAddons(srcPath.string());
    vector<string> availableAddons;
    scanAddons(tcRoot, availableAddons);
    vector<int> addonSelected(availableAddons.size(), 0);
    vector<string> missingAddons;
    for (const string& want : requestedAddons) {
        bool found = false;
        for (size_t i = 0; i < availableAddons.size(); ++i) {
            if (availableAddons[i] == want) {
                addonSelected[i] = 1;
                found = true;
                break;
            }
        }
        if (!found) missingAddons.push_back(want);
    }
    // Preserve missing addons by appending them as "selected" entries. They
    // won't be in TRUSSC/addons/, but ProjectGenerator::writeAddonsMake will
    // still emit them, so the destination's addons.make mirrors the source's.
    for (const string& m : missingAddons) {
        availableAddons.push_back(m);
        addonSelected.push_back(1);
    }

    string projectName = dstPath.filename().string();
    string projectDir = dstPath.parent_path().string();
    if (projectName.empty()) {
        cerr << "Error: could not derive project name from path '" << dstArg << "'\n";
        return 1;
    }

    bool useGit = !noGit && isGitWorkTree(srcPath);

    cout << "Copying " << srcPath.string() << " -> " << dstPath.string()
         << " [" << (useGit ? "git" : "skip-list")
         << (dryRun ? ", dry-run" : "") << "]\n";

    if (useGit) {
        vector<string> files = gitListFiles(srcPath);
        if (files.empty()) {
            cerr << "Error: `git ls-files` returned nothing under " << srcPath.string()
                 << ". Is this a valid TrussC project?\n";
            return 1;
        }
        if (dryRun) {
            for (const string& f : files) cout << "  " << f << "\n";
            cout << "(" << files.size() << " file" << (files.size() == 1 ? "" : "s") << ")\n";
        } else {
            try {
                fs::create_directories(projectDir);
                fs::create_directories(dstPath);
                for (const string& rel : files) {
                    fs::path from = srcPath / rel;
                    fs::path to = dstPath / rel;
                    // git ls-files may list entries that no longer exist on disk
                    // (rare, but safer to skip than to fail).
                    if (!fs::exists(from)) continue;
                    fs::create_directories(to.parent_path());
                    if (fs::is_symlink(from) || fs::is_regular_file(from)) {
                        fs::copy_file(from, to);
                    } else if (fs::is_directory(from)) {
                        fs::copy(from, to, fs::copy_options::recursive);
                    }
                }
            } catch (const fs::filesystem_error& e) {
                cerr << "Error: copy failed: " << e.what() << "\n";
                return 1;
            }
        }
    } else {
        // Non-git: enumerate top-level entries with the built-in skip list, then
        // special-case bin/data/ so user assets survive even though bin/ is skipped.
        vector<fs::path> toCopy;
        for (const auto& entry : fs::directory_iterator(srcPath)) {
            string name = entry.path().filename().string();
            if (isSkippedInCp(name)) continue;
            toCopy.push_back(entry.path());
        }
        fs::path srcBinData = srcPath / "bin" / "data";
        bool hasBinData = fs::exists(srcBinData);

        if (dryRun) {
            for (const auto& p : toCopy) {
                string name = p.filename().string();
                cout << "  " << name << (fs::is_directory(p) ? "/" : "") << "\n";
            }
            if (hasBinData) cout << "  bin/data/ (preserved)\n";
            else            cout << "  bin/data/ (created empty)\n";
        } else {
            try {
                fs::create_directories(projectDir);
                fs::create_directories(dstPath);
                for (const auto& src : toCopy) {
                    fs::copy(src, dstPath / src.filename().string(),
                             fs::copy_options::recursive);
                }
                fs::path dstBinData = dstPath / "bin" / "data";
                if (hasBinData) {
                    fs::create_directories(dstPath / "bin");
                    fs::copy(srcBinData, dstBinData, fs::copy_options::recursive);
                } else {
                    fs::create_directories(dstBinData);
                    ofstream gitkeep((dstBinData / ".gitkeep").string());
                }
            } catch (const fs::filesystem_error& e) {
                cerr << "Error: copy failed: " << e.what() << "\n";
                return 1;
            }
        }
    }

    if (dryRun) {
        cout << "\n(dry run) Would regenerate at " << dstPath.string() << ":\n"
             << "  CMakeLists.txt\n"
             << "  CMakePresets.json\n"
             << "  addons.make (" << requestedAddons.size() << " addon"
             << (requestedAddons.size() == 1 ? "" : "s") << ")\n"
             << "  IDE files for: " << ideStr << "\n";
        if (!missingAddons.empty()) {
            cout << "\n(dry run) Warning: these addons are referenced by the source "
                 << "but not present under " << tcRoot << "/addons/:\n";
            for (const string& m : missingAddons) cout << "  " << m << "\n";
        }
        return 0;
    }

    ProjectSettings settings;
    settings.tcRoot = tcRoot;
    settings.projectName = projectName;
    settings.projectDir = projectDir;
    settings.addons = availableAddons;
    settings.addonSelected = addonSelected;
    settings.generateWebBuild = web;
    settings.generateAndroidBuild = android;
    settings.generateIosBuild = ios;
    settings.detectBuildEnvironment();

    if (!parseIdeType(ideStr, settings.ideType)) {
        cerr << "Error: unknown IDE type '" << ideStr
             << "'. Valid: vscode, cursor, xcode, vs, cmake\n";
        return 1;
    }

    if (!missingAddons.empty()) {
        cerr << "Warning: these addons are referenced by the source but not "
             << "present under " << tcRoot << "/addons/:\n";
        for (const string& m : missingAddons) cerr << "  " << m << "\n";
        cerr << "They were preserved in addons.make. CMake configure will fail "
             << "until you clone them (e.g. `trusscli addon clone <name>`).\n";
    }

    cout << "Regenerating build files for new location...\n";
    ProjectGenerator gen(settings);
    gen.setLogCallback([](const string& msg) { cout << msg << endl; });
    string err = gen.update(dstPath.string());
    if (!err.empty()) {
        if (!missingAddons.empty()) {
            cerr << "Note: regen failed, likely due to missing addons above. "
                 << "Files are still at " << dstPath.string() << ". "
                 << "Clone the addons then run `trusscli update -p "
                 << dstPath.string() << "`.\n";
            return 0;
        }
        cerr << "Error: " << err << "\n";
        return 1;
    }

    cout << "Project copied: " << dstPath.string() << "\n";
    return 0;
}

// =============================================================================
// Subcommand: update (regenerate project build files)
// =============================================================================

static void printUpdateHelp() {
    cout << "Usage: trusscli update [options]\n"
         << "\n"
         << "Regenerate build files (CMakeLists.txt, CMakePresets.json, IDE files)\n"
         << "for the TrussC project in the current directory. The addon list is\n"
         << "read from the existing addons.make.\n"
         << "\n"
         << "Options:\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "      --web                  Enable Web build\n"
         << "      --android              Enable Android build\n"
         << "      --ios                  Enable iOS build\n"
         << "      --ide <type>           IDE: vscode, cursor, xcode, vs, cmake\n"
         << "      --tc-root <path>       Path to TrussC root directory\n"
         << "  -h, --help                 Show this help\n";
}

static int cmdUpdate(const vector<string>& args) {
    string projectPath;
    bool web = false, android = false, ios = false;
    string ideStr = "vscode";
    string tcRoot;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printUpdateHelp(); return 0; }
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, projectPath)) return 1;
        }
        else if (a == "--web") web = true;
        else if (a == "--android") android = true;
        else if (a == "--ios") ios = true;
        else if (a == "--ide") {
            if (!needValue(i, a, ideStr)) return 1;
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else {
            cerr << "Error: unknown argument '" << a << "'\n"
                 << "Run 'trusscli update --help' for usage.\n";
            return 1;
        }
    }

    string resolvedProjectPath;
    string resolvedTcRoot;
    if (int rc = resolveProjectAndTcRoot(projectPath, tcRoot, resolvedProjectPath, resolvedTcRoot)) {
        return rc;
    }
    projectPath = resolvedProjectPath;
    tcRoot = resolvedTcRoot;

    vector<string> availableAddons;
    scanAddons(tcRoot, availableAddons);

    ProjectSettings settings;
    settings.tcRoot = tcRoot;
    settings.projectName = fs::canonical(projectPath).filename().string();
    settings.addons = availableAddons;
    parseAddonsMake(projectPath, availableAddons, settings.addonSelected);
    settings.generateWebBuild = web;
    settings.generateAndroidBuild = android;
    settings.generateIosBuild = ios;
    settings.detectBuildEnvironment();

    if (!parseIdeType(ideStr, settings.ideType)) {
        cerr << "Error: unknown IDE type '" << ideStr
             << "'. Valid: vscode, cursor, xcode, vs, cmake\n";
        return 1;
    }

    settings.templatePath = tcRoot + "/examples/templates/emptyExample";

    if (int rc = runProjectUpdate(settings, projectPath)) return rc;
    cout << "Project updated: " << projectPath << "\n";
    return 0;
}

// =============================================================================
// Subcommand: upgrade (update TrussC itself)
// =============================================================================

static void printUpgradeHelp() {
    cout << "Usage: trusscli upgrade [options]\n"
         << "\n"
         << "Upgrade TrussC to the latest version by pulling the latest code from\n"
         << "the git repository and rebuilding trusscli. The /usr/local/bin symlink\n"
         << "(if present) automatically points to the new binary.\n"
         << "\n"
         << "On Windows, the rebuild step is skipped — you'll be asked to run the\n"
         << "build script manually.\n"
         << "\n"
         << "Options:\n"
         << "      --tc-root <path>       Path to TrussC root directory\n"
         << "  -h, --help                 Show this help\n";
}

static int cmdUpgrade(const vector<string>& args) {
    string tcRoot;

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printUpgradeHelp(); return 0; }
        else if (a == "--tc-root") {
            if (i + 1 >= args.size()) {
                cerr << "Error: --tc-root requires a value\n";
                return 1;
            }
            tcRoot = args[++i];
        }
        else {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli upgrade --help' for usage.\n";
            return 1;
        }
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root. Use --tc-root <path>.\n";
        return 1;
    }

    // Step 1: git pull
    cout << "Upgrading TrussC (" << tcRoot << ") ...\n";
    int rc = runProcess({"git", "-C", tcRoot, "pull"});
    if (rc != 0) {
        cerr << "Error: git pull failed (exit code " << rc << ").\n";
        return 1;
    }

    // Step 2: rebuild trusscli
#ifdef _WIN32
    cout << "\nGit pull succeeded. Please rebuild trusscli manually:\n"
         << "  cd " << tcRoot << "\\tools\n"
         << "  build_win.bat\n";
    return 0;
#else
    cout << "\nRebuilding trusscli ...\n";
    string cmake = findCMake();
    string toolsDir = tcRoot + "/tools";
    string buildDir = toolsDir + "/build";

    fs::create_directories(buildDir);

    rc = runProcess({cmake, "-S", toolsDir, "-B", buildDir});
    if (rc != 0) {
        cerr << "Error: cmake configure failed.\n";
        return 1;
    }

    rc = runProcess({cmake, "--build", buildDir, "--parallel"});
    if (rc != 0) {
        cerr << "Error: build failed.\n";
        return 1;
    }

    cout << "\ntrusscli upgraded successfully.\n"
         << "The new version will be used on the next invocation.\n";
    return 0;
#endif
}

// =============================================================================
// Subcommand: add
// =============================================================================

static void printAddHelp() {
    cout << "Usage: trusscli addon add <addon> [<addon>...]\n"
         << "\n"
         << "Add one or more addons to the TrussC project in the current directory.\n"
         << "The project is detected by walking up from CWD. The addons.make file\n"
         << "is updated and the build files are regenerated.\n"
         << "\n"
         << "Options:\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "      --tc-root <path>       Path to TrussC root directory\n"
         << "  -h, --help                 Show this help\n"
         << "\n"
         << "Examples:\n"
         << "  trusscli addon add tcxOsc                      Add a single addon\n"
         << "  trusscli addon add tcxOsc tcxImGui tcxBox2d    Add multiple addons at once\n";
}

static int cmdAdd(const vector<string>& args) {
    vector<string> requestedAddons;
    string explicitPath;
    string tcRoot;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printAddHelp(); return 0; }
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, explicitPath)) return 1;
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli add --help' for usage.\n";
            return 1;
        }
        else {
            requestedAddons.push_back(a);
        }
    }

    if (requestedAddons.empty()) {
        cerr << "Error: 'add' requires at least one addon name\n"
             << "Usage: trusscli add <addon> [<addon>...]\n";
        return 1;
    }

    string projectPath, resolvedTcRoot;
    if (int rc = resolveProjectAndTcRoot(explicitPath, tcRoot, projectPath, resolvedTcRoot)) {
        return rc;
    }

    vector<string> availableAddons;
    scanAddons(resolvedTcRoot, availableAddons);

    // Validate requested addons against the available set
    for (const string& want : requestedAddons) {
        if (find(availableAddons.begin(), availableAddons.end(), want) == availableAddons.end()) {
            cerr << "Error: addon '" << want << "' is not available locally.\n"
                 << "Clone it first:  trusscli addon clone " << want << "\n"
                 << "Or list remote addons:  trusscli addon list --remote\n";
            return 1;
        }
    }

    // Read current selection, add the requested addons (idempotent — duplicates warn)
    vector<int> addonSelected;
    parseAddonsMake(projectPath, availableAddons, addonSelected);

    vector<string> addedNow;
    for (const string& want : requestedAddons) {
        for (size_t i = 0; i < availableAddons.size(); ++i) {
            if (availableAddons[i] == want) {
                if (addonSelected[i]) {
                    cout << "  (already present) " << want << "\n";
                } else {
                    addonSelected[i] = 1;
                    addedNow.push_back(want);
                }
                break;
            }
        }
    }

    // Build the new addons list in the order they appear in availableAddons
    vector<string> selectedNames;
    for (size_t i = 0; i < availableAddons.size(); ++i) {
        if (addonSelected[i]) selectedNames.push_back(availableAddons[i]);
    }

    if (!writeAddonsMake(projectPath, selectedNames)) return 1;

    if (addedNow.empty()) {
        cout << "No changes to addons.make. Regenerating build files anyway...\n";
    } else {
        cout << "Added " << addedNow.size() << " addon(s): ";
        for (size_t i = 0; i < addedNow.size(); ++i) {
            if (i > 0) cout << ", ";
            cout << addedNow[i];
        }
        cout << "\n";
    }

    // Regenerate
    ProjectSettings settings;
    settings.tcRoot = resolvedTcRoot;
    settings.projectName = fs::canonical(projectPath).filename().string();
    settings.addons = availableAddons;
    settings.addonSelected = addonSelected;
    settings.detectBuildEnvironment();
    settings.templatePath = resolvedTcRoot + "/examples/templates/emptyExample";

    if (int rc = runProjectUpdate(settings, projectPath)) return rc;
    cout << "Project updated: " << projectPath << "\n";
    return 0;
}

// =============================================================================
// Subcommand: remove
// =============================================================================

static void printRemoveHelp() {
    cout << "Usage: trusscli addon remove <addon> [<addon>...]\n"
         << "\n"
         << "Remove one or more addons from the TrussC project in the current\n"
         << "directory. The project is detected by walking up from CWD. The\n"
         << "addons.make file is updated and the build files are regenerated.\n"
         << "\n"
         << "Options:\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "      --tc-root <path>       Path to TrussC root directory\n"
         << "  -h, --help                 Show this help\n"
         << "\n"
         << "Examples:\n"
         << "  trusscli addon remove tcxOsc              Remove a single addon\n"
         << "  trusscli addon remove tcxOsc tcxImGui     Remove multiple addons at once\n";
}

static int cmdRemove(const vector<string>& args) {
    vector<string> requestedAddons;
    string explicitPath;
    string tcRoot;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printRemoveHelp(); return 0; }
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, explicitPath)) return 1;
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli remove --help' for usage.\n";
            return 1;
        }
        else {
            requestedAddons.push_back(a);
        }
    }

    if (requestedAddons.empty()) {
        cerr << "Error: 'remove' requires at least one addon name\n"
             << "Usage: trusscli remove <addon> [<addon>...]\n";
        return 1;
    }

    string projectPath, resolvedTcRoot;
    if (int rc = resolveProjectAndTcRoot(explicitPath, tcRoot, projectPath, resolvedTcRoot)) {
        return rc;
    }

    vector<string> availableAddons;
    scanAddons(resolvedTcRoot, availableAddons);

    // Read current selection
    vector<int> addonSelected;
    parseAddonsMake(projectPath, availableAddons, addonSelected);

    vector<string> removedNow;
    for (const string& want : requestedAddons) {
        bool found = false;
        for (size_t i = 0; i < availableAddons.size(); ++i) {
            if (availableAddons[i] == want) {
                found = true;
                if (addonSelected[i]) {
                    addonSelected[i] = 0;
                    removedNow.push_back(want);
                } else {
                    cout << "  (not present) " << want << "\n";
                }
                break;
            }
        }
        if (!found) {
            // Unknown addon name. Could be a typo — warn but don't fail.
            cout << "  (unknown addon, not in " << resolvedTcRoot << "/addons/) " << want << "\n";
        }
    }

    // Build the new addons list in the order they appear in availableAddons
    vector<string> selectedNames;
    for (size_t i = 0; i < availableAddons.size(); ++i) {
        if (addonSelected[i]) selectedNames.push_back(availableAddons[i]);
    }

    if (!writeAddonsMake(projectPath, selectedNames)) return 1;

    if (removedNow.empty()) {
        cout << "No changes to addons.make. Regenerating build files anyway...\n";
    } else {
        cout << "Removed " << removedNow.size() << " addon(s): ";
        for (size_t i = 0; i < removedNow.size(); ++i) {
            if (i > 0) cout << ", ";
            cout << removedNow[i];
        }
        cout << "\n";
    }

    ProjectSettings settings;
    settings.tcRoot = resolvedTcRoot;
    settings.projectName = fs::canonical(projectPath).filename().string();
    settings.addons = availableAddons;
    settings.addonSelected = addonSelected;
    settings.detectBuildEnvironment();
    settings.templatePath = resolvedTcRoot + "/examples/templates/emptyExample";

    if (int rc = runProjectUpdate(settings, projectPath)) return rc;
    cout << "Project updated: " << projectPath << "\n";
    return 0;
}

// =============================================================================
// Addon registry
// =============================================================================

static const char* kRegistryURL =
    "https://raw.githubusercontent.com/TrussC-org/trussc-addons/gh-pages/registry.json";

struct AddonInfo {
    string name;
    string url;        // git clone URL
    string version;
    string description;
    string author;
};

// Fetch the addon registry from GitHub. Returns empty vector on network error.
static vector<AddonInfo> fetchRegistry() {
    cout << "Fetching addon registry ...\n";
    auto [code, out] = captureCommand("curl -sf \"" + string(kRegistryURL) + "\"");
    if (code != 0 || out.empty()) {
        cerr << "Error: could not fetch addon registry.\n"
             << "URL: " << kRegistryURL << "\n"
             << "Check your network connection.\n";
        return {};
    }
    try {
        Json data = Json::parse(out);
        vector<AddonInfo> result;
        for (const auto& a : data["addons"]) {
            result.push_back({
                a.value("name", ""),
                a.value("url", ""),
                a.value("version", "unknown"),
                a.value("description", ""),
                a.value("author", "")
            });
        }
        return result;
    } catch (...) {
        cerr << "Error: failed to parse registry JSON.\n";
        return {};
    }
}

// =============================================================================
// Subcommand: addon list
// =============================================================================

static int cmdAddonList(const vector<string>& args) {
    bool showRemote = false;
    string explicitPath;
    string tcRoot;

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") {
            cout << "Usage: trusscli addon list [options]\n"
                 << "\n"
                 << "List addons. By default shows locally available addons and which\n"
                 << "are installed in the current project. Use --remote to also show\n"
                 << "addons available from the registry.\n"
                 << "\n"
                 << "Options:\n"
                 << "      --remote               Include addons from the online registry\n"
                 << "  -p, --path <path>          Project path (for installed check)\n"
                 << "      --tc-root <path>       TrussC root directory\n"
                 << "  -h, --help                 Show this help\n";
            return 0;
        }
        else if (a == "--remote") showRemote = true;
        else if (a == "-p" || a == "--path") {
            if (++i >= args.size()) { cerr << "Error: " << a << " requires a value\n"; return 1; }
            explicitPath = args[i];
        }
        else if (a == "--tc-root") {
            if (++i >= args.size()) { cerr << "Error: --tc-root requires a value\n"; return 1; }
            tcRoot = args[i];
        }
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root.\n";
        return 1;
    }

    // Scan local addons
    vector<string> localAddons;
    scanAddons(tcRoot, localAddons);

    // Check which are installed in the current project
    string projectPath = explicitPath.empty() ? autoDetectProjectRoot("") : explicitPath;
    vector<string> installedAddons;
    if (!projectPath.empty()) {
        installedAddons = readProjectAddons(projectPath);
    }
    auto isInstalled = [&](const string& name) {
        return find(installedAddons.begin(), installedAddons.end(), name) != installedAddons.end();
    };
    auto isLocal = [&](const string& name) {
        return find(localAddons.begin(), localAddons.end(), name) != localAddons.end();
    };

    // Display local addons
    if (!localAddons.empty()) {
        for (const string& name : localAddons) {
            string status = isInstalled(name) ? "[installed]" : "[available]";
            cout << "  " << status << "   " << name << "\n";
        }
    } else {
        cout << "  (no local addons found)\n";
    }

    // Optionally show remote addons
    if (showRemote) {
        auto registry = fetchRegistry();
        if (registry.empty()) return 1;

        bool hasRemote = false;
        for (const auto& addon : registry) {
            if (!isLocal(addon.name)) {
                if (!hasRemote) {
                    cout << "\n  Remote (use 'trusscli addon clone <name>' to download):\n";
                    hasRemote = true;
                }
                cout << "  [remote]      " << addon.name;
                if (!addon.version.empty() && addon.version != "unknown") {
                    cout << "  " << addon.version;
                }
                if (!addon.description.empty()) {
                    cout << "  " << addon.description;
                }
                cout << "\n";
            }
        }
        if (!hasRemote) {
            cout << "\n  All registry addons are already available locally.\n";
        }
    }

    return 0;
}

// =============================================================================
// Subcommand: addon clone
// =============================================================================

static int cmdAddonClone(const vector<string>& args) {
    vector<string> addonNames;
    string tcRoot;

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") {
            cout << "Usage: trusscli addon clone <addon> [<addon>...]\n"
                 << "\n"
                 << "Clone addon(s) from the registry into the TrussC addons/ folder.\n"
                 << "After cloning, use 'trusscli addon add <addon>' to add it to a project.\n"
                 << "\n"
                 << "Options:\n"
                 << "      --tc-root <path>       TrussC root directory\n"
                 << "  -h, --help                 Show this help\n";
            return 0;
        }
        else if (a == "--tc-root") {
            if (++i >= args.size()) { cerr << "Error: --tc-root requires a value\n"; return 1; }
            tcRoot = args[i];
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n"; return 1;
        }
        else {
            addonNames.push_back(a);
        }
    }

    if (addonNames.empty()) {
        cerr << "Error: 'addon clone' requires at least one addon name\n"
             << "Usage: trusscli addon clone <addon> [<addon>...]\n";
        return 1;
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root.\n";
        return 1;
    }

    auto registry = fetchRegistry();
    if (registry.empty()) return 1;

    int failures = 0;
    for (const string& name : addonNames) {
        // Find in registry
        const AddonInfo* found = nullptr;
        for (const auto& a : registry) {
            if (a.name == name) { found = &a; break; }
        }
        if (!found) {
            cerr << "Error: '" << name << "' not found in registry.\n"
                 << "Run 'trusscli addon list --remote' to see available addons.\n";
            failures++;
            continue;
        }

        string targetDir = tcRoot + "/addons/" + name;
        if (fs::exists(targetDir)) {
            cout << "  (already exists) " << name << " → " << targetDir << "\n";
            continue;
        }

        cout << "Cloning " << name << " ...\n";
        int rc = runProcess({"git", "clone", found->url, targetDir});
        if (rc != 0) {
            cerr << "Error: git clone failed for " << name << "\n";
            failures++;
        } else {
            cout << "  Cloned " << name << " → " << targetDir << "\n";
        }
    }

    return failures > 0 ? 1 : 0;
}

// =============================================================================
// Subcommand: addon pull
// =============================================================================

static int cmdAddonPull(const vector<string>& args) {
    vector<string> addonNames;
    bool pullAll = false;
    string tcRoot;

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") {
            cout << "Usage: trusscli addon pull [<addon>...] [--all]\n"
                 << "\n"
                 << "Update addon(s) by running git pull in their directory.\n"
                 << "Only affects addons that were cloned via git (have a .git/ folder).\n"
                 << "\n"
                 << "Options:\n"
                 << "      --all                  Pull all git-based addons\n"
                 << "      --tc-root <path>       TrussC root directory\n"
                 << "  -h, --help                 Show this help\n";
            return 0;
        }
        else if (a == "--all") pullAll = true;
        else if (a == "--tc-root") {
            if (++i >= args.size()) { cerr << "Error: --tc-root requires a value\n"; return 1; }
            tcRoot = args[i];
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n"; return 1;
        }
        else {
            addonNames.push_back(a);
        }
    }

    if (addonNames.empty() && !pullAll) {
        cerr << "Error: specify addon name(s) or use --all\n"
             << "Usage: trusscli addon pull [<addon>...] [--all]\n";
        return 1;
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root.\n";
        return 1;
    }

    string addonsDir = tcRoot + "/addons";
    if (!fs::exists(addonsDir)) {
        cerr << "Error: addons directory not found at " << addonsDir << "\n";
        return 1;
    }

    // If --all, collect all git-based addons
    if (pullAll) {
        addonNames.clear();
        for (const auto& entry : fs::directory_iterator(addonsDir)) {
            if (entry.is_directory() &&
                fs::exists(entry.path() / ".git")) {
                addonNames.push_back(entry.path().filename().string());
            }
        }
        sort(addonNames.begin(), addonNames.end());
        if (addonNames.empty()) {
            cout << "No git-based addons found in " << addonsDir << "\n";
            return 0;
        }
    }

    int failures = 0;
    for (const string& name : addonNames) {
        string addonDir = addonsDir + "/" + name;
        if (!fs::exists(addonDir)) {
            cerr << "  (not found) " << name << "\n";
            failures++;
            continue;
        }
        if (!fs::exists(addonDir + "/.git")) {
            cout << "  (not a git repo, skipping) " << name << "\n";
            continue;
        }
        cout << "Pulling " << name << " ...\n";
        int rc = runProcess({"git", "-C", addonDir, "pull"});
        if (rc != 0) {
            cerr << "  Error: git pull failed for " << name << "\n";
            failures++;
        }
    }

    return failures > 0 ? 1 : 0;
}

// =============================================================================
// Subcommand: addon search
// =============================================================================

static int cmdAddonSearch(const vector<string>& args) {
    string query;

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") {
            cout << "Usage: trusscli addon search <query>\n"
                 << "\n"
                 << "Search the addon registry by name or description.\n";
            return 0;
        }
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n"; return 1;
        }
        else {
            if (!query.empty()) query += " ";
            query += a;
        }
    }

    if (query.empty()) {
        cerr << "Error: 'addon search' requires a search query\n"
             << "Usage: trusscli addon search <query>\n";
        return 1;
    }

    auto registry = fetchRegistry();
    if (registry.empty()) return 1;

    // Case-insensitive search
    string lowerQuery = query;
    transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    vector<const AddonInfo*> matches;
    for (const auto& a : registry) {
        string lowerName = a.name;
        transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        string lowerDesc = a.description;
        transform(lowerDesc.begin(), lowerDesc.end(), lowerDesc.begin(), ::tolower);

        if (lowerName.find(lowerQuery) != string::npos ||
            lowerDesc.find(lowerQuery) != string::npos) {
            matches.push_back(&a);
        }
    }

    if (matches.empty()) {
        cout << "No addons matching '" << query << "' found in registry.\n";
        return 0;
    }

    cout << matches.size() << " addon(s) matching '" << query << "':\n\n";
    for (const auto* a : matches) {
        cout << "  " << a->name;
        if (!a->version.empty() && a->version != "unknown") cout << "  " << a->version;
        cout << "\n";
        if (!a->description.empty()) cout << "    " << a->description << "\n";
        if (!a->author.empty()) cout << "    by " << a->author << "\n";
        cout << "    " << a->url << "\n\n";
    }

    return 0;
}

// =============================================================================
// Subcommand: addon (dispatches to add / remove / list / clone / pull / search)
// =============================================================================

static void printAddonHelp() {
    cout << "Usage: trusscli addon <command> [options]\n"
         << "\n"
         << "Manage addons for TrussC projects.\n"
         << "\n"
         << "Commands:\n"
         << "  list                       List addons (local + optionally remote)\n"
         << "  add <addon>...             Add addons to the project\n"
         << "  remove <addon>...          Remove addons from the project\n"
         << "  clone <addon>...           Clone addons from the registry\n"
         << "  pull [<addon>...] [--all]  Update addons via git pull\n"
         << "  search <query>             Search the addon registry\n"
         << "\n"
         << "Run 'trusscli addon <command> --help' for command-specific help.\n";
}

static int cmdAddon(const vector<string>& args) {
    if (args.empty()) {
        printAddonHelp();
        return 0;
    }
    const string& sub = args[0];
    if (sub == "-h" || sub == "--help" || sub == "help") {
        printAddonHelp();
        return 0;
    }
    vector<string> subArgs(args.begin() + 1, args.end());
    if (sub == "list")   return cmdAddonList(subArgs);
    if (sub == "add")    return cmdAdd(subArgs);
    if (sub == "remove") return cmdRemove(subArgs);
    if (sub == "clone")  return cmdAddonClone(subArgs);
    if (sub == "pull")   return cmdAddonPull(subArgs);
    if (sub == "search") return cmdAddonSearch(subArgs);

    cerr << "Error: unknown addon command '" << sub << "'\n"
         << "Run 'trusscli addon --help' for usage.\n";
    return 1;
}

// =============================================================================
// Subcommand: info
// =============================================================================

enum class InfoFormat { Human, Plain, Json };
enum class InfoSection { All, Project, Targets, Addons };

static void printInfoHelp() {
    cout << "Usage: trusscli info [section] [options]\n"
         << "\n"
         << "Show information about the TrussC project in the current directory.\n"
         << "If no project is found, prints framework info instead (TrussC root,\n"
         << "available addons).\n"
         << "\n"
         << "Sections:\n"
         << "  (default)                  All sections\n"
         << "  project                    Project name, path, TrussC root\n"
         << "  targets                    Enabled build targets\n"
         << "  addons                     Addons listed in addons.make\n"
         << "\n"
         << "Options:\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "      --tc-root <path>       Path to TrussC root directory\n"
         << "      --json                 Machine-readable JSON output\n"
         << "      --format <fmt>         Output format: human, plain, json (default: human)\n"
         << "  -h, --help                 Show this help\n"
         << "\n"
         << "Examples:\n"
         << "  trusscli info                    Full human-readable overview\n"
         << "  trusscli info addons             Just the addon list\n"
         << "  trusscli info --json             Full info as JSON\n"
         << "  trusscli info addons --format plain   Grep-friendly addon list\n";
}

// Parse the CMakePresets.json of a project and return the list of configure
// preset names. Returns empty vector on any error.
static vector<string> readEnabledTargets(const string& projectPath) {
    vector<string> out;
    string presetsPath = projectPath + "/CMakePresets.json";
    if (!fs::exists(presetsPath)) return out;
    try {
        ifstream file(presetsPath);
        Json data = Json::parse(file);
        if (data.contains("configurePresets") && data["configurePresets"].is_array()) {
            for (const auto& p : data["configurePresets"]) {
                if (p.contains("name") && p["name"].is_string()) {
                    out.push_back(p["name"].get<string>());
                }
            }
        }
    } catch (...) {
        // Silently ignore parse errors — info is read-only and best-effort.
    }
    return out;
}

// Read the addons listed in addons.make (in order).
static vector<string> readProjectAddons(const string& projectPath) {
    vector<string> out;
    string path = projectPath + "/addons.make";
    if (!fs::exists(path)) return out;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        string name = line.substr(start, end - start + 1);
        if (name.empty() || name[0] == '#') continue;
        out.push_back(name);
    }
    return out;
}

static int cmdInfo(const vector<string>& args) {
    string explicitPath;
    string tcRoot;
    InfoFormat format = InfoFormat::Human;
    InfoSection section = InfoSection::All;
    bool sectionSet = false;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printInfoHelp(); return 0; }
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, explicitPath)) return 1;
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else if (a == "--json") {
            format = InfoFormat::Json;
        }
        else if (a == "--format") {
            string v;
            if (!needValue(i, a, v)) return 1;
            if      (v == "human") format = InfoFormat::Human;
            else if (v == "plain") format = InfoFormat::Plain;
            else if (v == "json")  format = InfoFormat::Json;
            else {
                cerr << "Error: unknown --format '" << v << "'. Valid: human, plain, json\n";
                return 1;
            }
        }
        else if (!a.empty() && a[0] == '-') {
            // Accept --format json style
            const string prefix = "--format=";
            if (a.rfind(prefix, 0) == 0) {
                string v = a.substr(prefix.size());
                if      (v == "human") format = InfoFormat::Human;
                else if (v == "plain") format = InfoFormat::Plain;
                else if (v == "json")  format = InfoFormat::Json;
                else {
                    cerr << "Error: unknown --format '" << v << "'. Valid: human, plain, json\n";
                    return 1;
                }
                continue;
            }
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli info --help' for usage.\n";
            return 1;
        }
        else {
            if (sectionSet) {
                cerr << "Error: 'info' takes a single section argument\n";
                return 1;
            }
            if      (a == "all")     section = InfoSection::All;
            else if (a == "project") section = InfoSection::Project;
            else if (a == "targets") section = InfoSection::Targets;
            else if (a == "addons")  section = InfoSection::Addons;
            else {
                cerr << "Error: unknown section '" << a << "'. Valid: project, targets, addons, all\n";
                return 1;
            }
            sectionSet = true;
        }
    }

    // Resolve project path — empty if not inside a project.
    string projectPath;
    if (!explicitPath.empty()) {
        if (!fs::is_directory(explicitPath)) {
            cerr << "Error: project path '" << explicitPath << "' does not exist\n";
            return 1;
        }
        projectPath = explicitPath;
    } else {
        projectPath = autoDetectProjectRoot("");
    }

    // Resolve TC_ROOT.
    string resolvedTcRoot = tcRoot.empty() ? autoDetectTcRoot() : tcRoot;
    if (resolvedTcRoot.empty()) {
        cerr << "Error: could not detect TrussC root. Use --tc-root <path> or set TRUSSC_DIR.\n";
        return 1;
    }

    // Gather data.
    string projectName;
    vector<string> targets;
    vector<string> addons;
    bool inProject = !projectPath.empty();
    if (inProject) {
        projectName = fs::canonical(projectPath).filename().string();
        targets = readEnabledTargets(projectPath);
        addons = readProjectAddons(projectPath);
    } else {
        // Framework mode: list available addons from tcRoot
        scanAddons(resolvedTcRoot, addons);
    }

    // JSON output
    if (format == InfoFormat::Json) {
        Json out = Json::object();
        if (inProject) {
            out["project"] = {
                {"name", projectName},
                {"path", fs::absolute(projectPath).string()},
                {"tc_root", fs::absolute(resolvedTcRoot).string()}
            };
            out["targets"] = targets;
            out["addons"] = addons;
        } else {
            out["project"] = nullptr;
            out["tc_root"] = fs::absolute(resolvedTcRoot).string();
            out["available_addons"] = addons;
        }
        if (section == InfoSection::Addons) {
            cout << (inProject ? out["addons"] : out["available_addons"]).dump(2) << "\n";
        } else if (section == InfoSection::Targets) {
            cout << (inProject ? out["targets"] : Json::array()).dump(2) << "\n";
        } else if (section == InfoSection::Project) {
            cout << (inProject ? out["project"] : Json()).dump(2) << "\n";
        } else {
            cout << out.dump(2) << "\n";
        }
        return 0;
    }

    // Plain output — one value per line, no decoration (grep-friendly)
    if (format == InfoFormat::Plain) {
        if (section == InfoSection::Addons || section == InfoSection::All) {
            for (const string& a : addons) cout << a << "\n";
        }
        if (section == InfoSection::Targets && inProject) {
            for (const string& t : targets) cout << t << "\n";
        }
        if (section == InfoSection::Project && inProject) {
            cout << "name=" << projectName << "\n"
                 << "path=" << fs::absolute(projectPath).string() << "\n"
                 << "tc_root=" << fs::absolute(resolvedTcRoot).string() << "\n";
        }
        return 0;
    }

    // Human output — pretty sections
    if (!inProject) {
        cout << "Not inside a TrussC project.\n\n"
             << "TrussC root: " << fs::absolute(resolvedTcRoot).string() << "\n\n";
        if (section == InfoSection::All || section == InfoSection::Addons) {
            cout << "Available addons (" << addons.size() << "):\n";
            for (const string& a : addons) cout << "  " << a << "\n";
        }
        return 0;
    }

    if (section == InfoSection::All || section == InfoSection::Project) {
        cout << "Project: " << projectName << "\n"
             << "Path:    " << fs::absolute(projectPath).string() << "\n"
             << "TrussC:  " << fs::absolute(resolvedTcRoot).string() << "\n";
    }

    if (section == InfoSection::All || section == InfoSection::Targets) {
        if (section == InfoSection::All) cout << "\n";
        if (targets.empty()) {
            cout << "Targets: (none — run 'trusscli update' to generate CMakePresets.json)\n";
        } else {
            cout << "Targets (" << targets.size() << "):\n";
            for (const string& t : targets) cout << "  " << t << "\n";
        }
    }

    if (section == InfoSection::All || section == InfoSection::Addons) {
        if (section == InfoSection::All) cout << "\n";
        if (addons.empty()) {
            cout << "Addons:  (none)\n";
        } else {
            cout << "Addons (" << addons.size() << "):\n";
            for (const string& a : addons) cout << "  " << a << "\n";
        }
    }

    return 0;
}

// =============================================================================
// Subcommand: doctor
// =============================================================================

static void printDoctorHelp() {
    cout << "Usage: trusscli doctor [options]\n"
         << "\n"
         << "Check your development environment for TrussC build prerequisites.\n"
         << "By default shows only essential checks and any failures. Use --verbose\n"
         << "to see all checks including optional tools and cross-compile targets.\n"
         << "\n"
         << "Options:\n"
         << "      --verbose              Show all checks (including OK / optional / skipped)\n"
         << "      --json                 Machine-readable JSON output\n"
         << "  -p, --path <path>          Check against a specific project's target list\n"
         << "      --tc-root <path>       Path to TrussC root directory\n"
         << "  -h, --help                 Show this help\n";
}

static int cmdDoctor(const vector<string>& args) {
    bool verbose = false;
    bool jsonOut = false;
    string explicitPath;
    string tcRoot;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printDoctorHelp(); return 0; }
        else if (a == "--verbose") verbose = true;
        else if (a == "--json") jsonOut = true;
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, explicitPath)) return 1;
        }
        else if (a == "--tc-root") {
            if (!needValue(i, a, tcRoot)) return 1;
        }
        else {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli doctor --help' for usage.\n";
            return 1;
        }
    }

    // Resolve TC_ROOT (best-effort for doctor — we still check even if missing)
    string resolvedTcRoot = tcRoot.empty() ? autoDetectTcRoot() : tcRoot;

    // Detect project targets (if inside a project)
    string projectPath;
    if (!explicitPath.empty()) {
        projectPath = explicitPath;
    } else {
        projectPath = autoDetectProjectRoot("");
    }

    vector<string> targets;
    if (!projectPath.empty()) {
        targets = readEnabledTargets(projectPath);
    }
    auto hasTarget = [&](const string& t) {
        return find(targets.begin(), targets.end(), t) != targets.end();
    };

    // Run essential checks
    vector<CheckResult> results;
    results.push_back(checkCMake());
    results.push_back(checkCompiler());
    results.push_back(checkTrussCCore(resolvedTcRoot));
    results.push_back(checkPlatformSDK());

    // Cross-compile checks: only run if the project targets them
    bool checkWeb = hasTarget("web");
    bool checkAndroid = hasTarget("android");

    if (checkWeb || verbose) {
        auto r = checkEmscripten();
        if (!checkWeb) r.status = CheckStatus::Skipped;
        results.push_back(r);
    }
    if (checkAndroid || verbose) {
        auto r = checkAndroidNDK();
        if (!checkAndroid) r.status = CheckStatus::Skipped;
        results.push_back(r);
    }

    // Optional tools
    if (verbose) {
        results.push_back(checkNinja());
        results.push_back(checkGit());
    }

    // Count issues
    int errors = 0, warnings = 0;
    for (const auto& r : results) {
        if (r.status == CheckStatus::Error) errors++;
        if (r.status == CheckStatus::Warning) warnings++;
    }

    // JSON output
    if (jsonOut) {
        Json out = Json::array();
        for (const auto& r : results) {
            Json item;
            item["name"] = r.name;
            item["status"] = r.status == CheckStatus::OK      ? "ok"
                           : r.status == CheckStatus::Warning  ? "warning"
                           : r.status == CheckStatus::Error    ? "error"
                           : "skipped";
            item["detail"] = r.detail;
            if (!r.hint.empty()) item["hint"] = r.hint;
            out.push_back(item);
        }
        cout << out.dump(2) << "\n";
        return errors > 0 ? 1 : 0;
    }

    // Human output
    auto statusIcon = [](CheckStatus s) -> const char* {
        switch (s) {
            case CheckStatus::OK:      return "[OK]";
            case CheckStatus::Warning: return "[??]";
            case CheckStatus::Error:   return "[NG]";
            case CheckStatus::Skipped: return "[--]";
        }
        return "[??]";
    };

    for (const auto& r : results) {
        // In default mode: skip OK non-essential items, skip Skipped items
        if (!verbose) {
            if (r.status == CheckStatus::Skipped) continue;
            if (r.status == CheckStatus::OK && !r.essential) continue;
        }

        cout << "  " << statusIcon(r.status)
             << " " << r.name;
        // Pad to align detail
        int pad = 20 - (int)r.name.size();
        if (pad > 0) cout << string(pad, ' ');
        if (!r.detail.empty()) cout << r.detail;
        cout << "\n";
        if (!r.hint.empty() && (r.status != CheckStatus::OK || verbose)) {
            cout << "       -> " << r.hint << "\n";
        }
    }

    cout << "\n";
    if (errors == 0 && warnings == 0) {
        cout << "No issues found.";
        if (!verbose) cout << " Run 'trusscli doctor --verbose' for full details.";
        cout << "\n";
    } else {
        if (errors > 0) cout << errors << " error(s)";
        if (errors > 0 && warnings > 0) cout << ", ";
        if (warnings > 0) cout << warnings << " warning(s)";
        cout << " found.";
        if (!verbose) cout << " Run 'trusscli doctor --verbose' for full details.";
        cout << "\n";
    }

    return errors > 0 ? 1 : 0;
}

// =============================================================================
// Subcommand: build
// =============================================================================

// Detect the native CMake preset name at compile time.
#if defined(__APPLE__) && !TARGET_OS_IPHONE
static constexpr const char* kNativePreset = "macos";
#elif defined(_WIN32)
static constexpr const char* kNativePreset = "windows";
#elif defined(__linux__)
static constexpr const char* kNativePreset = "linux";
#else
static constexpr const char* kNativePreset = nullptr;
#endif

// Find the cmake binary. On macOS, PATH may not contain cmake when launched
// from Finder, so check common Homebrew paths as well.
static string findCMake() {
#ifdef __APPLE__
    const char* paths[] = {
        "/opt/homebrew/bin/cmake",
        "/usr/local/bin/cmake",
        "/Applications/CMake.app/Contents/bin/cmake",
    };
    for (const char* p : paths) {
        if (fs::exists(p)) return p;
    }
#endif
    return "cmake";
}

static void printBuildHelp() {
    cout << "Usage: trusscli build [options]\n"
         << "\n"
         << "Build the TrussC project in the current directory. By default builds\n"
         << "for the native platform; use --web / --android / --ios to cross-compile.\n"
         << "\n"
         << "Options:\n"
         << "      --web                  Build for WebAssembly\n"
         << "      --android              Build for Android\n"
         << "      --ios                  Build for iOS\n"
         << "      --release              Build in Release configuration\n"
         << "      --clean                Clean before building (--clean-first)\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "  -h, --help                 Show this help\n";
}

// =============================================================================
// Subcommand: clean
// =============================================================================

static void printCleanHelp() {
    cout << "Usage: trusscli clean [options]\n"
         << "\n"
         << "Delete build directories for the TrussC project in the current directory.\n"
         << "By default deletes the native platform build directory. Use --all to\n"
         << "delete all build directories (web, android, ios, etc.).\n"
         << "\n"
         << "Options:\n"
         << "      --all                  Delete all build directories\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "  -h, --help                 Show this help\n";
}

static int cmdClean(const vector<string>& args) {
    string explicitPath;
    bool cleanAll = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printCleanHelp(); return 0; }
        else if (a == "--all") cleanAll = true;
        else if (a == "-p" || a == "--path") {
            if (++i >= args.size()) { cerr << "Error: " << a << " requires a value\n"; return 1; }
            explicitPath = args[i];
        }
        else {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli clean --help' for usage.\n";
            return 1;
        }
    }

    string projectPath = explicitPath.empty() ? autoDetectProjectRoot("") : explicitPath;
    if (projectPath.empty()) {
        cerr << "Error: not inside a TrussC project.\n";
        return 1;
    }

    const char* buildDirs[] = {
        "build-macos", "build-linux", "build-windows",
        "build-web", "build-android", "build-ios",
        "build"
    };

    int removed = 0;
    for (const char* dir : buildDirs) {
        string fullPath = projectPath + "/" + dir;
        if (fs::exists(fullPath)) {
            if (!cleanAll && string(dir) != string("build-") + kNativePreset && string(dir) != "build") {
                continue;  // skip non-native dirs unless --all
            }
            cout << "  Removing " << dir << "/\n";
            fs::remove_all(fullPath);
            removed++;
        }
    }

    if (removed == 0) {
        cout << "Nothing to clean.\n";
    } else {
        cout << "Cleaned " << removed << " build director" << (removed == 1 ? "y" : "ies") << ".\n";
    }
    return 0;
}

// =============================================================================
// Subcommand: build
// =============================================================================

static int cmdBuild(const vector<string>& args) {
    string explicitPath;
    string targetPreset;
    bool release = false;
    bool clean = false;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printBuildHelp(); return 0; }
        else if (a == "--web")     targetPreset = "web";
        else if (a == "--android") targetPreset = "android";
        else if (a == "--ios")     targetPreset = "ios";
        else if (a == "--release") release = true;
        else if (a == "--clean")   clean = true;
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, explicitPath)) return 1;
        }
        else {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli build --help' for usage.\n";
            return 1;
        }
    }

    string projectPath = explicitPath.empty() ? autoDetectProjectRoot("") : explicitPath;
    if (projectPath.empty()) {
        cerr << "Error: not inside a TrussC project.\n"
             << "Use 'trusscli build -p <path>' or run from inside a project.\n";
        return 1;
    }

    if (targetPreset.empty()) {
        if (!kNativePreset) {
            cerr << "Error: cannot detect native platform. Use --web, --android, or --ios.\n";
            return 1;
        }
        targetPreset = kNativePreset;
    }

    // Check that the preset exists in the project
    string presetsFile = projectPath + "/CMakePresets.json";
    if (!fs::exists(presetsFile)) {
        cerr << "Error: no CMakePresets.json in " << projectPath << "\n"
             << "Run 'trusscli update' first to generate build files.\n";
        return 1;
    }

    string cmake = findCMake();

    // Determine parallel job count. On Linux, limit based on available RAM
    // (~1.5 GB per compile job) to prevent OOM on memory-constrained SBCs.
    string parallelArg = "--parallel";
#ifdef __linux__
    {
        ifstream meminfo("/proc/meminfo");
        string line;
        long availKB = 0;
        while (getline(meminfo, line)) {
            if (line.rfind("MemAvailable:", 0) == 0) {
                sscanf(line.c_str(), "MemAvailable: %ld", &availKB);
                break;
            }
        }
        if (availKB > 0) {
            const long kbPerJob = 1572864; // 1.5 GiB
            int jobs = max(1, (int)(availKB / kbPerJob));
            // Cap at CPU count + 2
            long cpus = sysconf(_SC_NPROCESSORS_ONLN);
            if (cpus > 0 && jobs > (int)cpus + 2) jobs = (int)cpus + 2;
            // Only show message if RAM is the limiting factor
            if (availKB < kbPerJob * (cpus > 0 ? cpus : 4)) {
                float availGB = availKB / 1048576.0f;
                cout << "  * Parallel jobs limited to " << jobs
                     << " (" << fixed << setprecision(1) << availGB
                     << " GB RAM available, ~1.5 GB per job)\n";
            }
            parallelArg = "--parallel=" + to_string(jobs);
        }
    }
#endif

    // cmake --build --preset <target> --parallel[=N] [--config Release] [--clean-first]
    vector<string> cmd = {cmake, "--build", "--preset", targetPreset, parallelArg};
    if (release) { cmd.push_back("--config"); cmd.push_back("Release"); }
    if (clean) cmd.push_back("--clean-first");

    // Run from the project directory
    string savedCwd = fs::current_path().string();
    fs::current_path(projectPath);
    int rc = runProcess(cmd);
    fs::current_path(savedCwd);

    if (rc != 0) {
        cerr << "Build failed (exit code " << rc << ").\n";
    }
    return rc;
}

// =============================================================================
// Subcommand: run
// =============================================================================

static void printRunHelp() {
    cout << "Usage: trusscli run [options]\n"
         << "\n"
         << "Build and launch the TrussC project in the current directory.\n"
         << "Default: native build + launch. Use --web / --android / --ios to\n"
         << "cross-compile and launch on the appropriate target.\n"
         << "\n"
         << "Options:\n"
         << "      --web                  Build WASM and start a local HTTP server\n"
         << "      --android              Build, adb install, and launch    [not yet implemented]\n"
         << "      --ios                  Build and run in iOS Simulator    [not yet implemented]\n"
         << "      --session <backend>    Launch inside a display session (Linux, no desktop).\n"
         << "                             Backends: labwc, x11. Example: --session labwc\n"
         << "      --release              Build in Release configuration\n"
         << "  -p, --path <path>          Operate on a specific project path\n"
         << "  -h, --help                 Show this help\n";
}

static int cmdRun(const vector<string>& args) {
    string explicitPath;
    string target; // "", "web", "android", "ios"
    string session; // display session backend: "labwc", "x11", ""
    bool release = false;

    auto needValue = [&](size_t& i, const string& opt, string& out) -> bool {
        if (i + 1 >= args.size()) {
            cerr << "Error: " << opt << " requires a value\n";
            return false;
        }
        out = args[++i];
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const string& a = args[i];
        if (a == "-h" || a == "--help") { printRunHelp(); return 0; }
        else if (a == "--web")      target = "web";
        else if (a == "--android")  target = "android";
        else if (a == "--ios")      target = "ios";
        else if (a == "--session") {
            if (!needValue(i, a, session)) return 1;
            if (session != "labwc" && session != "x11") {
                cerr << "Error: unknown session backend '" << session << "'\n"
                     << "Available: labwc, x11\n";
                return 1;
            }
        }
        else if (a == "--release")  release = true;
        else if (a == "-p" || a == "--path") {
            if (!needValue(i, a, explicitPath)) return 1;
        }
        else {
            cerr << "Error: unknown option '" << a << "'\n"
                 << "Run 'trusscli run --help' for usage.\n";
            return 1;
        }
    }

    string projectPath = explicitPath.empty() ? autoDetectProjectRoot("") : explicitPath;
    if (projectPath.empty()) {
        cerr << "Error: not inside a TrussC project.\n"
             << "Use 'trusscli run -p <path>' or run from inside a project.\n";
        return 1;
    }

    string projectName = fs::canonical(projectPath).filename().string();

    // Android / iOS: not yet implemented — check before building
    if (target == "android") {
        cerr << "Error: 'run --android' is not yet implemented (adb install + launch).\n"
             << "For now, build with 'trusscli build --android' and deploy manually.\n"
             << "See https://github.com/TrussC-org/TrussC/issues/24\n";
        return 1;
    }
    if (target == "ios") {
        cerr << "Error: 'run --ios' is not yet implemented (xcrun simctl).\n"
             << "For now, build with 'trusscli build --ios' and run from Xcode.\n"
             << "See https://github.com/TrussC-org/TrussC/issues/24\n";
        return 1;
    }

    // --- Build phase ---
    vector<string> buildArgs;
    if (!target.empty()) buildArgs.push_back("--" + target);
    if (release) buildArgs.push_back("--release");
    if (!explicitPath.empty()) { buildArgs.push_back("-p"); buildArgs.push_back(explicitPath); }

    int buildRc = cmdBuild(buildArgs);
    if (buildRc != 0) return buildRc;

    cout << "\n";

    // --- Launch phase ---

    // Web: use emrun or python http.server
    if (target == "web") {
        string htmlPath = projectPath + "/bin/" + projectName + ".html";
        if (!fs::exists(htmlPath)) {
            cerr << "Error: built HTML not found at " << htmlPath << "\n";
            return 1;
        }
        cout << "Launching web server for " << htmlPath << " ...\n";
        // Try emrun first (Emscripten's built-in server)
        auto [emrunCode, emrunOut] = captureCommand("which emrun 2>/dev/null");
        if (emrunCode == 0 && !emrunOut.empty()) {
            return runProcess({"emrun", htmlPath});
        }
        // Fallback: python3 http.server
        cout << "emrun not found, using python3 http.server ...\n";
        string binDir = projectPath + "/bin";
        cout << "Open http://localhost:8000/" << projectName << ".html in your browser.\n";
        return runProcess({"python3", "-m", "http.server", "8000", "-d", binDir});
    }

    // Native: find and launch the binary
#ifdef __APPLE__
    string appPath = projectPath + "/bin/" + projectName + ".app";
    if (fs::exists(appPath)) {
        string binPath = appPath + "/Contents/MacOS/" + projectName;
        if (!session.empty()) {
            cerr << "Warning: --session is a Linux-only option (ignored on macOS).\n";
        }
        cout << "Launching " << projectName << " ...\n";
        return runProcess({binPath});
    }
#elif defined(__linux__)
    string binPath = projectPath + "/bin/" + projectName;
    if (fs::exists(binPath)) {
        if (session == "labwc") {
            cout << "Launching via labwc Wayland session ...\n";
            return runProcess({"labwc", "-s", binPath});
        }
        if (session == "x11") {
            cout << "Launching via xinit (X11 session) ...\n";
            return runProcess({"xinit", binPath});
        }
        cout << "Launching " << projectName << " ...\n";
        return runProcess({binPath});
    }
#elif defined(_WIN32)
    string exePath = projectPath + "/bin/" + projectName + ".exe";
    if (fs::exists(exePath)) {
        if (!session.empty()) {
            cerr << "Warning: --session is a Linux-only option (ignored on Windows).\n";
        }
        cout << "Launching " << projectName << " ...\n";
        return runProcess({exePath});
    }
#endif

    cerr << "Error: built binary not found in " << projectPath << "/bin/\n"
         << "Make sure the build succeeded.\n";
    return 1;
}

// =============================================================================
// Subcommand: completion
// =============================================================================

static const char* kZshCompletion = R"ZSH(#compdef trusscli

_trusscli() {
    local -a commands addon_commands
    commands=(
        'new:Create a new project'
        'cp:Copy an existing project'
        'update:Regenerate build files'
        'upgrade:Upgrade TrussC (git pull + rebuild)'
        'addon:Manage addons'
        'info:Show project / framework info'
        'doctor:Check development environment'
        'clean:Delete build directories'
        'build:Build the project'
        'run:Build and launch the project'
        'completion:Generate shell completion script'
    )
    addon_commands=(
        'list:List addons'
        'add:Add addons to the project'
        'remove:Remove addons from the project'
        'clone:Clone addons from the registry'
        'pull:Update addons via git pull'
        'search:Search the addon registry'
    )

    if (( CURRENT == 2 )); then
        _describe 'command' commands
        return
    fi

    case "$words[2]" in
        addon)
            if (( CURRENT == 3 )); then
                _describe 'addon command' addon_commands
            elif (( CURRENT >= 4 )); then
                case "$words[3]" in
                    add|remove)
                        local -a addons
                        addons=(${(f)"$(ls -d addons/tcx* 2>/dev/null | xargs -n1 basename 2>/dev/null)"})
                        if [[ -n "$addons" ]]; then
                            _describe 'addon' addons
                        fi
                        ;;
                    clone|search)
                        # No completion for these (require registry or free text)
                        ;;
                esac
            fi
            ;;
        new)
            _files -/
            ;;
        cp)
            local -a cp_opts
            cp_opts=('-n:Dry run' '--dry-run:Dry run' '--no-git:Use built-in skip list'
                     '--web:Enable web' '--android:Enable android' '--ios:Enable ios'
                     '--ide:IDE type' '--tc-root:TrussC root')
            _describe 'option' cp_opts
            _files -/
            ;;
        update|build|run|clean)
            local -a opts
            case "$words[2]" in
                update) opts=('-p:Project path' '--path:Project path' '--web:Enable web' '--android:Enable android' '--ios:Enable ios' '--ide:IDE type' '--tc-root:TrussC root') ;;
                build)  opts=('--web:Web build' '--android:Android build' '--ios:iOS build' '--release:Release config' '--clean:Clean first' '-p:Project path' '--path:Project path') ;;
                run)    opts=('--web:Web' '--android:Android' '--ios:iOS' '--session:Display session' '--release:Release' '-p:Project path' '--path:Project path') ;;
                clean)  opts=('--all:Delete all build dirs' '-p:Project path' '--path:Project path') ;;
            esac
            _describe 'option' opts
            ;;
        info)
            if (( CURRENT == 3 )); then
                local -a sections
                sections=('project' 'targets' 'addons' 'all')
                _describe 'section' sections
            fi
            ;;
        completion)
            if (( CURRENT == 3 )); then
                _describe 'shell' '(zsh bash)'
            fi
            ;;
    esac
}

compdef _trusscli trusscli
)ZSH";

static const char* kBashCompletion = R"BASH(
_trusscli() {
    local cur prev words cword
    _init_completion 2>/dev/null || {
        COMPREPLY=()
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
        cword=$COMP_CWORD
    }

    if [[ $cword -eq 1 ]]; then
        COMPREPLY=($(compgen -W "new cp update upgrade addon info doctor clean build run completion" -- "$cur"))
        return
    fi

    case "${COMP_WORDS[1]}" in
        addon)
            if [[ $cword -eq 2 ]]; then
                COMPREPLY=($(compgen -W "list add remove clone pull search" -- "$cur"))
            elif [[ $cword -ge 3 ]]; then
                case "${COMP_WORDS[2]}" in
                    add|remove)
                        local addons=$(ls -d addons/tcx* 2>/dev/null | xargs -n1 basename 2>/dev/null)
                        COMPREPLY=($(compgen -W "$addons" -- "$cur"))
                        ;;
                esac
            fi
            ;;
        new)
            COMPREPLY=($(compgen -d -- "$cur"))
            ;;
        cp)
            case "$prev" in
                --ide) COMPREPLY=($(compgen -W "vscode cursor xcode vs cmake" -- "$cur")); return ;;
                --tc-root) COMPREPLY=($(compgen -d -- "$cur")); return ;;
            esac
            if [[ $cur == -* ]]; then
                COMPREPLY=($(compgen -W "-n --dry-run --no-git --web --android --ios --ide --tc-root" -- "$cur"))
            else
                COMPREPLY=($(compgen -d -- "$cur"))
            fi
            ;;
        update|build|run|clean)
            case "$prev" in
                -p|--path|--tc-root)
                    COMPREPLY=($(compgen -d -- "$cur"))
                    return
                    ;;
                --ide)
                    COMPREPLY=($(compgen -W "vscode cursor xcode vs cmake" -- "$cur"))
                    return
                    ;;
                --session)
                    COMPREPLY=($(compgen -W "labwc x11" -- "$cur"))
                    return
                    ;;
            esac
            case "${COMP_WORDS[1]}" in
                update) COMPREPLY=($(compgen -W "-p --path --web --android --ios --ide --tc-root" -- "$cur")) ;;
                build)  COMPREPLY=($(compgen -W "--web --android --ios --release --clean -p --path" -- "$cur")) ;;
                run)    COMPREPLY=($(compgen -W "--web --android --ios --session --release -p --path" -- "$cur")) ;;
                clean)  COMPREPLY=($(compgen -W "--all -p --path" -- "$cur")) ;;
            esac
            ;;
        info)
            if [[ $cword -eq 2 ]]; then
                COMPREPLY=($(compgen -W "project targets addons all --json --format" -- "$cur"))
            fi
            ;;
        completion)
            if [[ $cword -eq 2 ]]; then
                COMPREPLY=($(compgen -W "zsh bash" -- "$cur"))
            fi
            ;;
    esac
}

complete -o default -F _trusscli trusscli
)BASH";

static int cmdCompletion(const vector<string>& args) {
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        cout << "Usage: trusscli completion <shell>\n"
             << "\n"
             << "Generate shell completion script. Add to your shell config:\n"
             << "\n"
             << "  # zsh (~/.zshrc)\n"
             << "  eval \"$(trusscli completion zsh)\"\n"
             << "\n"
             << "  # bash (~/.bashrc)\n"
             << "  eval \"$(trusscli completion bash)\"\n";
        return args.empty() ? 1 : 0;
    }
    if (args[0] == "zsh") {
        cout << kZshCompletion;
        return 0;
    }
    if (args[0] == "bash") {
        cout << kBashCompletion;
        return 0;
    }
    cerr << "Error: unknown shell '" << args[0] << "'. Available: zsh, bash\n";
    return 1;
}

// =============================================================================
// Top-level help
// =============================================================================

static void printTopHelp() {
    cout << "trusscli — TrussC project tool\n"
         << "\n"
         << "Usage:\n"
         << "  trusscli <command> [options]\n"
         << "  trusscli                       Launch the GUI (TrussC Project Generator)\n"
         << "\n"
         << "Commands:\n"
         << "  new <path>                     Create a new project at <path>\n"
         << "  cp <src> <dst>                 Copy an existing project to <dst>\n"
         << "  update                         Regenerate build files for the project in CWD\n"
         << "  upgrade                        Upgrade TrussC (git pull + rebuild trusscli)\n"
         << "  addon <add|remove>             Manage addons\n"
         << "  info [section]                 Show project / framework info\n"
         << "  doctor                         Check development environment\n"
         << "  clean                          Delete build directories\n"
         << "  build                          Build the project\n"
         << "  run                            Build and launch the project\n"
         << "\n"
         << "Common options (per subcommand):\n"
         << "  -p, --path <path>              Operate on a specific project path\n"
         << "      --tc-root <path>           Path to TrussC root directory\n"
         << "  -h, --help                     Show command-specific help\n"
         << "\n"
         << "Examples:\n"
         << "  trusscli new myApp                        Create ./myApp\n"
         << "  trusscli new ./apps/myApp --web           Create with Web build enabled\n"
         << "  trusscli new myApp -a tcxOsc -a tcxIME    With addons\n"
         << "  trusscli addon add tcxOsc                 Add an addon to existing project\n"
         << "  trusscli addon remove tcxOsc              Remove an addon\n"
         << "  trusscli update                           Regenerate the project in CWD\n"
         << "  trusscli update -p ./apps/myApp           Regenerate a specific project\n"
         << "\n"
         << "Run 'trusscli <command> --help' for command-specific help.\n";
}

// =============================================================================
// main: subcommand dispatch
// =============================================================================

int main(int argc, char* argv[]) {
    vector<string> args(argv + 1, argv + argc);

    // No args → launch the GUI (TrussC Project Generator)
    if (args.empty()) {
        #ifdef __linux__
        const char* display = std::getenv("DISPLAY");
        const char* wayland = std::getenv("WAYLAND_DISPLAY");
        if ((!display || display[0] == '\0') &&
            (!wayland || wayland[0] == '\0')) {
            cerr << "Error: no display server found "
                 << "(DISPLAY and WAYLAND_DISPLAY are unset).\n"
                 << "Run 'trusscli --help' for CLI usage.\n";
            return 1;
        }
        #endif
        WindowSettings settings;
        settings.title = "TrussC Project Generator";
        settings.width = 500;
        settings.height = 600;
        return runApp<tcApp>(settings);
    }

    // Top-level help (also accept bare `help` for friendliness)
    const string& first = args[0];
    if (first == "-h" || first == "--help" || first == "help") {
        printTopHelp();
        return 0;
    }

    // Dispatch
    vector<string> subArgs(args.begin() + 1, args.end());
    if (first == "new")     return cmdNew(subArgs);
    if (first == "cp")      return cmdCp(subArgs);
    if (first == "update")  return cmdUpdate(subArgs);
    if (first == "upgrade") return cmdUpgrade(subArgs);
    if (first == "addon")  return cmdAddon(subArgs);
    if (first == "info")   return cmdInfo(subArgs);
    if (first == "doctor") return cmdDoctor(subArgs);
    if (first == "clean")  return cmdClean(subArgs);
    if (first == "build")      return cmdBuild(subArgs);
    if (first == "run")        return cmdRun(subArgs);
    if (first == "completion") return cmdCompletion(subArgs);

    cerr << "Error: unknown command '" << first << "'\n"
         << "Run 'trusscli --help' for usage.\n";
    return 1;
}
