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
    fs::path searchPath = fs::path(getExecutablePath()).parent_path();
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
// Subcommand: update
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
         << "      --tc-root <path>       Path to TrussC root directory (auto-detected by default)\n"
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
            cerr << "Error: unknown argument '" << a << "'\n";
            cerr << "Run 'trusscli update --help' for usage.\n";
            return 1;
        }
    }

    if (projectPath.empty()) {
        projectPath = autoDetectProjectRoot("");
        if (projectPath.empty()) {
            cerr << "Error: not inside a TrussC project (no CMakeLists.txt + "
                 << "addons.make found in CWD or any parent).\n";
            cerr << "Use 'trusscli update -p <path>' or run from inside a project.\n";
            return 1;
        }
    } else if (!fs::is_directory(projectPath)) {
        cerr << "Error: project path '" << projectPath << "' does not exist\n";
        return 1;
    }

    if (tcRoot.empty()) tcRoot = autoDetectTcRoot();
    if (tcRoot.empty()) {
        cerr << "Error: could not detect TrussC root. "
             << "Use --tc-root <path> or set TRUSSC_DIR.\n";
        return 1;
    }

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

    // TODO: target flags (--web/--android/--ios) and --ide aren't persisted in
    // the project — running update without them resets to defaults. Persist
    // them in CMakePresets.json or a sidecar config so subsequent updates
    // remember the previous selection.
    if (!parseIdeType(ideStr, settings.ideType)) {
        cerr << "Error: unknown IDE type '" << ideStr
             << "'. Valid: vscode, cursor, xcode, vs, cmake\n";
        return 1;
    }

    settings.templatePath = tcRoot + "/examples/templates/emptyExample";

    ProjectGenerator gen(settings);
    gen.setLogCallback([](const string& msg) { cout << msg << endl; });
    string err = gen.update(projectPath);
    if (!err.empty()) {
        cerr << "Error: " << err << "\n";
        return 1;
    }
    cout << "Project updated: " << projectPath << "\n";
    return 0;
}

// =============================================================================
// Stub for unimplemented commands
// =============================================================================

static int cmdNotImplemented(const string& cmdName) {
    cerr << "Error: '" << cmdName << "' is not yet implemented.\n"
         << "See https://github.com/TrussC-org/TrussC/issues/24 for the design.\n";
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
         << "  update                         Regenerate build files for the project in CWD\n"
         << "  add <addon>...                 Add addons to the project          [not yet implemented]\n"
         << "  remove <addon>                 Remove an addon                    [not yet implemented]\n"
         << "  info                           Show project / framework info      [not yet implemented]\n"
         << "  build                          Build the project                  [not yet implemented]\n"
         << "  run                            Build and launch the project       [not yet implemented]\n"
         << "\n"
         << "Common options (per subcommand):\n"
         << "  -p, --path <path>              Operate on a specific project path\n"
         << "      --tc-root <path>           Path to TrussC root directory\n"
         << "  -h, --help                     Show command-specific help\n"
         << "\n"
         << "Examples:\n"
         << "  trusscli new myApp                        Create ./myApp\n"
         << "  trusscli new ./apps/myApp --web           Create with Web build enabled\n"
         << "  trusscli new myApp -a tcxOsc -a tcxIME    With addons (repeat form)\n"
         << "  trusscli new myApp -a tcxOsc,tcxIME       With addons (comma form)\n"
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
    if (first == "new")    return cmdNew(subArgs);
    if (first == "update") return cmdUpdate(subArgs);
    if (first == "add" || first == "remove" || first == "info" ||
        first == "build" || first == "run") {
        return cmdNotImplemented(first);
    }

    cerr << "Error: unknown command '" << first << "'\n"
         << "Run 'trusscli --help' for usage.\n";
    return 1;
}
