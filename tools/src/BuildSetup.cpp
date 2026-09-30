#include "BuildSetup.h"
#include <nlohmann/json.hpp>
#include <algorithm>

using namespace std;
using Json = nlohmann::json;

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

    if (!in.hasCache) {
        // `cmake --build` does not configure a missing or empty build folder
        // ("... is not a directory" / "could not load cache"), so configure
        // first. Without a build-type flag no -D is added and the
        // trussc_app.cmake default applies, as after `trusscli update`.
        string msg = "[build] No CMake cache in " + in.buildDir + ", configuring";
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

string presetCacheVariable(const string& presetsJson,
                           const string& preset,
                           const string& variable) {
    Json data = Json::parse(presetsJson, nullptr, /*allow_exceptions=*/false);
    if (!data.is_object()) return "";
    auto presets = data.find("configurePresets");
    if (presets == data.end() || !presets->is_array()) return "";
    for (const auto& p : *presets) {
        if (!p.is_object()) continue;
        auto name = p.find("name");
        if (name == p.end() || !name->is_string() || name->get<string>() != preset) continue;
        auto vars = p.find("cacheVariables");
        if (vars == p.end() || !vars->is_object()) return "";
        auto v = vars->find(variable);
        if (v == vars->end()) return "";
        const Json& value = v->is_object() ? v->value("value", Json()) : *v;
        return value.is_string() ? value.get<string>() : "";
    }
    return "";
}

bool shouldRefreshPresets(const string& nativePreset,
                          const string& targetPreset,
                          const ToolchainCheck& check) {
    return nativePreset == "windows" && targetPreset == nativePreset && check.stale();
}
