#include "BuildSetup.h"
#include "ProjectGenerator.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std;
using Json = nlohmann::json;
namespace fs = std::filesystem;

ConfigurePlan planConfigure(const ConfigureInputs& in) {
    ConfigurePlan plan;

    // --warnings: turn on -Wall/-Wextra for the app's own sources. The cache
    // var is sticky: it stays on for later builds until a configure without
    // it (or `trusscli update`) resets it.
    if (in.warnings) {
        plan.messages.push_back("[warnings] Enabling -Wall -Wextra for this project's sources...");
        plan.defines.push_back("-DTRUSSC_WARNINGS=ON");
        plan.configure = true;
    }

    // Build type on a single-config native preset: `cmake --build --config`
    // is ignored there, so the build type is pinned via CMAKE_BUILD_TYPE at
    // configure time. Only the native preset is managed: web / android bake
    // their own build type (MinSizeRel / Release) into the preset, and iOS
    // uses the multi-config Xcode generator, where --config applies.
    // The default (no flag) is RelWithDebInfo, the trussc_app.cmake default,
    // so a cache left on another type by an earlier flag goes back to it.
    string pinType;
    if (in.isNative) {
        const string effective = in.requestedBuildType.empty()
            ? "RelWithDebInfo" : in.requestedBuildType;
        const bool knownType = in.hasCache && !in.cachedBuildType.empty();
        if (knownType ? in.cachedBuildType != effective
                      : !in.requestedBuildType.empty()) {
            pinType = effective;
            plan.defines.push_back("-DCMAKE_BUILD_TYPE=" + effective);
            plan.configure = true;
        }
    }

    if (!in.hasCache || !in.generated) {
        // `cmake --build` does not configure a missing or empty build folder
        // ("... is not a directory" / "could not load cache"), so configure
        // first. A configure that failed leaves CMakeCache.txt but no build
        // system ("No rule to make target 'Makefile'"), so that folder is
        // configured again too. Without a build-type flag no -D is added and
        // the trussc_app.cmake default applies, as after `trusscli update`.
        string msg = in.hasCache
            ? "[build] No build files in " + in.buildDir +
                  " (an earlier configure did not finish), configuring"
            : "[build] No CMake cache in " + in.buildDir + ", configuring";
        if (!pinType.empty()) msg += " (CMAKE_BUILD_TYPE=" + pinType + ")";
        plan.messages.push_back(msg + "...");
        plan.configure = true;
    } else if (!pinType.empty()) {
        if (in.cachedBuildType.empty()) {
            plan.messages.push_back("[build] Configuring " + pinType +
                                    " build (CMAKE_BUILD_TYPE=" + pinType + ")...");
        } else {
            plan.messages.push_back("[build] Switching build type: " + in.cachedBuildType +
                                    " -> " + pinType + " ...");
        }
    }
    return plan;
}

// Cache entry value ("" when the file or entry is missing).
string readCMakeCacheValue(const string& cachePath, const string& key) {
    ifstream cache(cachePath);
    if (!cache) return "";
    string line;
    while (getline(cache, line)) {
        // Format: CMAKE_BUILD_TYPE:STRING=RelWithDebInfo
        if (line.rfind(key + ":", 0) == 0) {
            auto eq = line.find('=');
            if (eq != string::npos) return line.substr(eq + 1);
        }
    }
    return "";
}

string quoteWindowsArgument(const string& argument) {
    string quoted = "\"";
    size_t slashes = 0;
    for (char c : argument) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        // Windows argv parsing doubles backslashes before a quote.
        quoted.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        quoted += c;
        slashes = 0;
    }
    quoted.append(slashes * 2, '\\');
    return quoted + '"';
}

bool hasMainHeaderDependencies(const string& ninjaDeps) {
    istringstream lines(ninjaDeps);
    string line;
    bool found = false;
    while (getline(lines, line)) {
        const auto marker = line.find(": #deps ");
        if (marker == string::npos) continue;
        string object = line.substr(0, marker);
        replace(object.begin(), object.end(), '\\', '/');
        if (object.substr(object.find_last_of('/') + 1) != "main.cpp.obj") continue;
        istringstream countText(line.substr(marker + 8));
        long long count = 0;
        if (!(countText >> count) || count <= 0) return false;
        found = true;
    }
    return found;
}

// Whether a finished configure left a build system in dir. The generate
// step writes it only after the whole configure succeeded; a failed one
// leaves CMakeCache.txt and CMakeFiles/ alone.
static bool hasBuildSystem(const fs::path& dir) {
    error_code ec;
    if (fs::is_regular_file(dir / "Makefile", ec) ||
        fs::is_regular_file(dir / "build.ninja", ec)) {
        return true;
    }
    // Xcode (ios) and Visual Studio generators: <project>.xcodeproj / .sln
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const string ext = it->path().extension().string();
        if (ext == ".xcodeproj" || ext == ".sln") return true;
    }
    return false;
}

ConfigureInputs inspectBuildFolder(const string& projectDir,
                                   const string& targetPreset,
                                   const string& nativePreset) {
    ConfigureInputs in;
    in.buildDir = ProjectGenerator::buildDirForPreset(targetPreset);
    in.isNative = !nativePreset.empty() && targetPreset == nativePreset;
    const fs::path dir = fs::path(projectDir) / in.buildDir;
    error_code ec;
    in.hasCache = fs::is_regular_file(dir / "CMakeCache.txt", ec);
    in.generated = hasBuildSystem(dir);
    in.cachedBuildType = readCMakeCacheValue((dir / "CMakeCache.txt").string(), "CMAKE_BUILD_TYPE");
    return in;
}

vector<string> buildFoldersToClean(const string& nativePreset, bool all) {
    vector<string> dirs;
    if (all) {
        for (const string& preset : ProjectGenerator::allPresetNames()) {
            dirs.push_back(ProjectGenerator::buildDirForPreset(preset));
        }
    } else if (!nativePreset.empty()) {
        dirs.push_back(ProjectGenerator::buildDirForPreset(nativePreset));
    }
    dirs.push_back("build");
    return dirs;
}

vector<string> buildScriptsToClean(bool all) {
    vector<string> files;
    if (!all) return files;
    for (const string& preset : ProjectGenerator::allPresetNames()) {
        for (const string& script : ProjectGenerator::buildScriptsForPreset(preset)) {
            files.push_back(script);
        }
    }
    return files;
}

// Split a ';'-separated environment value into its entries.
static vector<string> splitPathList(const string& value) {
    vector<string> out;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(';', start);
        if (end == string::npos) end = value.size();
        if (end > start) out.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

// A CMake preset macro ($penv{PATH}, $env{X}, ${sourceDir}, $vendor{...})
// is expanded by CMake, not a pinned path.
static bool isPresetMacro(const string& entry) {
    return entry.find('$') != string::npos;
}

ToolchainCheck checkPresetToolchain(const string& presetsJson,
                                    const function<bool(const string&)>& exists) {
    ToolchainCheck result;
    Json data = Json::parse(presetsJson, nullptr, /*allow_exceptions=*/false);
    if (!data.is_object()) return result;
    auto presets = data.find("configurePresets");
    if (presets == data.end() || !presets->is_array()) return result;

    vector<string> pinned;
    for (const auto& p : *presets) {
        if (!p.is_object()) continue;
        auto name = p.find("name");
        if (name == p.end() || !name->is_string() || name->get<string>() != "windows") continue;

        auto vars = p.find("cacheVariables");
        if (vars != p.end() && vars->is_object()) {
            auto make = vars->find("CMAKE_MAKE_PROGRAM");
            if (make != vars->end()) {
                // A cache variable is a string or {"type": ..., "value": ...}
                const Json& v = make->is_object() ? make->value("value", Json()) : *make;
                if (v.is_string()) pinned.push_back(v.get<string>());
            }
        }
        auto env = p.find("environment");
        if (env != p.end() && env->is_object()) {
            for (const char* key : {"INCLUDE", "LIB", "PATH"}) {
                auto it = env->find(key);
                if (it == env->end() || !it->is_string()) continue;
                for (const string& entry : splitPathList(it->get<string>())) {
                    pinned.push_back(entry);
                }
            }
        }
    }

    for (const string& path : pinned) {
        if (path.empty() || isPresetMacro(path)) continue;
        result.pinned = true;
        if (!exists(path) &&
            find(result.missing.begin(), result.missing.end(), path) == result.missing.end()) {
            result.missing.push_back(path);
        }
    }
    return result;
}

static string toForwardSlash(string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return path;
}

bool canPinToolchain(const VsVersionInfo& vs) {
    return !vs.vcToolsVersion.empty() && !vs.windowsSdkVersion.empty();
}

WindowsToolchainPins windowsToolchainPins(const VsVersionInfo& vs) {
    WindowsToolchainPins pins;
    if (!vs.ninjaPath.empty()) pins.makeProgram = toForwardSlash(vs.ninjaPath);
    if (!canPinToolchain(vs)) return pins;

    const string vsPath = toForwardSlash(vs.installPath);
    const string& vcToolsVer = vs.vcToolsVersion;
    const string& sdkVer = vs.windowsSdkVersion;
    pins.include =
        vsPath + "/VC/Tools/MSVC/" + vcToolsVer + "/include;" +
        "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/ucrt;" +
        "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/shared;" +
        "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/um;" +
        "C:/Program Files (x86)/Windows Kits/10/Include/" + sdkVer + "/winrt";
    pins.lib =
        vsPath + "/VC/Tools/MSVC/" + vcToolsVer + "/lib/x64;" +
        "C:/Program Files (x86)/Windows Kits/10/Lib/" + sdkVer + "/ucrt/x64;" +
        "C:/Program Files (x86)/Windows Kits/10/Lib/" + sdkVer + "/um/x64";
    pins.path =
        vsPath + "/VC/Tools/MSVC/" + vcToolsVer + "/bin/Hostx64/x64;" +
        "C:/Program Files (x86)/Windows Kits/10/bin/" + sdkVer + "/x64;$penv{PATH}";
    return pins;
}

string repinWindowsToolchain(const string& presetsJson, const WindowsToolchainPins& pins) {
    Json data = Json::parse(presetsJson, nullptr, /*allow_exceptions=*/false);
    if (!data.is_object()) return "";
    auto presets = data.find("configurePresets");
    if (presets == data.end() || !presets->is_array()) return "";

    for (auto& p : *presets) {
        if (!p.is_object()) continue;
        auto name = p.find("name");
        if (name == p.end() || !name->is_string() || name->get<string>() != "windows") continue;

        // CMAKE_MAKE_PROGRAM (the other cache variables, e.g. TRUSSC_DIR, stay)
        if (!pins.makeProgram.empty()) {
            p["cacheVariables"]["CMAKE_MAKE_PROGRAM"] = pins.makeProgram;
        } else if (p.contains("cacheVariables") && p["cacheVariables"].is_object()) {
            p["cacheVariables"].erase("CMAKE_MAKE_PROGRAM");
        }

        // INCLUDE / LIB / PATH (other environment entries stay)
        const pair<const char*, const string*> envPins[] = {
            {"INCLUDE", &pins.include}, {"LIB", &pins.lib}, {"PATH", &pins.path},
        };
        for (const auto& [key, value] : envPins) {
            if (!value->empty()) {
                p["environment"][key] = *value;
            } else if (p.contains("environment") && p["environment"].is_object()) {
                p["environment"].erase(key);
            }
        }
        if (p.contains("environment") && p["environment"].is_object() &&
            p["environment"].empty()) {
            p.erase("environment");
        }
        // Same format as ProjectGenerator writes (saveJson, indent 2)
        return data.dump(2);
    }
    return "";
}

bool shouldRefreshPresets(const string& nativePreset,
                          const string& targetPreset,
                          const ToolchainCheck& check) {
    return nativePreset == "windows" && targetPreset == nativePreset && check.stale();
}
