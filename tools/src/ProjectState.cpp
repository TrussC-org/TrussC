#include "ProjectState.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;
using Json = nlohmann::json;

// Whether this OS can generate projects for `ide`. Matches the IDE list of
// the GUI (tcApp.cpp): Xcode on macOS only, Visual Studio on Windows only.
// Returns the OS name for the message when it cannot.
static const char* ideHostOnly(IdeType ide) {
#ifndef __APPLE__
    if (ide == IdeType::Xcode) return "macOS";
#endif
#ifndef _WIN32
    if (ide == IdeType::VisualStudio) return "Windows";
#endif
    (void)ide;
    return nullptr;
}

// The warning for an ios target off macOS, where writeCMakePresets() writes
// no ios preset. `source` names where it came from.
static string iosMacOnlyWarning(const string& source) {
    return "the ios target " + source + " is macOS only; dropped on this OS";
}

// TC_WEB_BACKEND of the web preset (a cacheVariables entry: a string, a
// boolean, null, or {"type": ..., "value": ...}), mapped the way CMake builds
// it (core/CMakeLists.txt, trussc_app.cmake): WebGPU when the variable is
// unset or exactly "WGPU", GLES3 (WebGL) for every other value.
static void readWebBackend(const Json& entry, PresetState& state) {
    const Json* value = &entry;
    if (entry.is_object()) {
        auto v = entry.find("value");
        if (v == entry.end()) {
            state.warnings.push_back("TC_WEB_BACKEND of the web preset has no \"value\"; "
                                     "ignored, so the web backend is WGPU (the default)");
            return;
        }
        value = &*v;
    }
    // null unsets the variable: CMake's default, WGPU
    if (value->is_null()) return;
    string text;
    if (value->is_string()) text = value->get<string>();
    else if (value->is_boolean()) text = value->get<bool>() ? "TRUE" : "FALSE";
    else {
        state.warnings.push_back("TC_WEB_BACKEND of the web preset is not a string; "
                                 "ignored, so the web backend is WGPU (the default)");
        return;
    }
    if (text == "WGPU") return;
    state.webBackend = 1;
    if (text != "GLES3") {
        state.warnings.push_back("TC_WEB_BACKEND \"" + text + "\" of the web preset is "
                                 "neither WGPU nor GLES3; CMake builds it as GLES3 (WebGL), "
                                 "so GLES3 is kept");
    }
}

// toolchainFile of the web / android preset, kept for a shell without the
// toolchain (see chooseToolchainFile()).
static void readToolchainFile(const Json& preset, const string& name, string& out,
                              PresetState& state) {
    auto file = preset.find("toolchainFile");
    if (file == preset.end()) return;
    if (!file->is_string()) {
        state.warnings.push_back("\"toolchainFile\" of the " + name + " preset is not a "
                                 "string; ignored");
        return;
    }
    out = file->get<string>();
}

PresetState parsePresetState(const string& jsonText) {
    PresetState state;
    Json data = Json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (!data.is_object()) {
        state.warnings.push_back("CMakePresets.json does not parse as a JSON object; "
                                 "its IDE and targets are ignored");
        return state;
    }
    state.found = true;

    auto presets = data.find("configurePresets");
    if (presets != data.end() && !presets->is_array()) {
        state.warnings.push_back("\"configurePresets\" in CMakePresets.json is not an "
                                 "array; its targets are ignored");
    }
    if (presets != data.end() && presets->is_array()) {
        [[maybe_unused]] bool droppedIos = false;
        for (const auto& p : *presets) {
            if (!p.is_object()) continue;
            auto name = p.find("name");
            if (name == p.end() || !name->is_string()) continue;
            const string n = name->get<string>();
            if (n == "web") {
                state.web = true;
                auto vars = p.find("cacheVariables");
                if (vars != p.end() && vars->is_object()) {
                    auto backend = vars->find("TC_WEB_BACKEND");
                    if (backend != vars->end()) readWebBackend(*backend, state);
                }
                readToolchainFile(p, n, state.webToolchainFile, state);
            }
            else if (n == "android") {
                state.android = true;
                readToolchainFile(p, n, state.androidToolchainFile, state);
            }
            else if (n == "ios") {
#ifdef __APPLE__
                state.ios = true;
#else
                // writeCMakePresets() writes the ios preset on macOS only, so
                // a kept one would vanish from the rewritten file unnoticed
                if (!droppedIos) {
                    state.warnings.push_back(iosMacOnlyWarning("in CMakePresets.json"));
                    droppedIos = true;
                }
#endif
            }
        }
    }

    // "vendor": {"trussc": {"ide": "<id>"}}. A missing entry (a file from a
    // trusscli before #350) is fine; a present but unusable one is reported.
    auto vendor = data.find("vendor");
    if (vendor == data.end()) return state;
    if (!vendor->is_object()) {
        state.ideWarning = "\"vendor\" in CMakePresets.json is not an object";
        return state;
    }
    auto trussc = vendor->find("trussc");
    if (trussc == vendor->end()) return state;
    if (!trussc->is_object()) {
        state.ideWarning = "\"vendor.trussc\" in CMakePresets.json is not an object";
        return state;
    }
    auto ide = trussc->find("ide");
    if (ide == trussc->end()) return state;
    if (!ide->is_string()) {
        state.ideWarning = "\"vendor.trussc.ide\" in CMakePresets.json is not a string";
        return state;
    }
    const string id = ide->get<string>();
    IdeType parsed;
    if (!IdeHelper::parseIdeId(id, parsed)) {
        state.ideWarning = "unknown IDE '" + id + "' in CMakePresets.json "
                           "(valid: vscode, cursor, xcode, vs, cmake)";
        return state;
    }
    if (const char* os = ideHostOnly(parsed)) {
        state.ideWarning = "IDE '" + id + "' in CMakePresets.json is " + os + " only";
        return state;
    }
    state.ide = parsed;
    state.hasIde = true;
    return state;
}

PresetState readPresetState(const string& projectPath) {
    fs::path path = fs::path(projectPath) / "CMakePresets.json";
    error_code ec;
    if (!fs::exists(path, ec)) return PresetState();
    ifstream file;
    if (fs::is_regular_file(path, ec)) file.open(path, ios::binary);
    if (!file.is_open()) {
        PresetState state;
        state.warnings.push_back("CMakePresets.json exists but cannot be read; "
                                 "its IDE and targets are ignored");
        return state;
    }
    stringstream ss;
    ss << file.rdbuf();
    return parsePresetState(ss.str());
}

bool parseTargetFlag(const string& arg, GenerationFlags& flags, string& errMsg) {
    struct Target { const char* name; optional<bool> GenerationFlags::* field; };
    static const Target targets[] = {
        {"web", &GenerationFlags::web},
        {"android", &GenerationFlags::android},
        {"ios", &GenerationFlags::ios},
    };
    for (const auto& t : targets) {
        const string on = string("--") + t.name;
        const string off = string("--no-") + t.name;
        if (arg != on && arg != off) continue;
        const bool value = (arg == on);
        optional<bool>& field = flags.*(t.field);
        if (field.has_value() && *field != value) {
            errMsg = "conflicting flags " + on + " and " + off;
        } else {
            field = value;
        }
        return true;
    }
    return false;
}

void applyGenerationOptions(ProjectSettings& settings,
                            const PresetState& state,
                            const GenerationFlags& flags) {
    if (state.found) {
        settings.generateWebBuild = state.web;
        settings.generateAndroidBuild = state.android;
        settings.generateIosBuild = state.ios;
        if (state.web) settings.webBackend = state.webBackend;
        if (state.hasIde) settings.ideType = state.ide;
        settings.savedWebToolchainFile = state.webToolchainFile;
        settings.savedAndroidToolchainFile = state.androidToolchainFile;
    }
    if (flags.web) settings.generateWebBuild = *flags.web;
    if (flags.android) settings.generateAndroidBuild = *flags.android;
    if (flags.ios) settings.generateIosBuild = *flags.ios;
    if (flags.ide) settings.ideType = *flags.ide;

    // A target from the presets that no flag of this run asked for
    settings.webKept = state.found && state.web && !flags.web.has_value();
    settings.androidKept = state.found && state.android && !flags.android.has_value();
    settings.iosKept = state.found && state.ios && !flags.ios.has_value();
}

RegenerationSetup prepareRegeneration(const string& projectPath,
                                      const string& tcRoot,
                                      const vector<string>& addons,
                                      const vector<int>& addonSelected,
                                      const GenerationFlags& flags) {
    RegenerationSetup setup;
    ProjectSettings& settings = setup.settings;
    settings.tcRoot = tcRoot;
    settings.projectName = fs::canonical(projectPath).filename().string();
    settings.addons = addons;
    settings.addonSelected = addonSelected;
    settings.templatePath = tcRoot + "/examples/templates/emptyExample";

    PresetState state = readPresetState(projectPath);
    applyGenerationOptions(settings, state, flags);
    settings.detectBuildEnvironment();

    // The file is rewritten, so saved settings that cannot be used would
    // otherwise vanish unnoticed.
    for (const string& w : state.warnings) {
        setup.warnings.push_back(w + ", and the file is rewritten.");
    }
#ifndef __APPLE__
    // An explicit --ios is dropped the same way as a saved ios target
    if (flags.ios.value_or(false)) {
        settings.generateIosBuild = false;
        setup.warnings.push_back(iosMacOnlyWarning("of --ios") + ".");
    }
#endif
    if (!state.ideWarning.empty() && !flags.ide) {
        setup.warnings.push_back(state.ideWarning + "; using the default IDE (" +
                                 IdeHelper::getIdeId(settings.ideType) + "). Choose one with "
                                 "'trusscli update --ide <type>'.");
    }
    // Show what the regeneration keeps whenever anything was read back
    if (state.found) {
        setup.summary = "Project settings: " + describeGenerationOptions(settings) +
                        " (kept from CMakePresets.json unless a flag changed them";
        if (!state.hasIde && !flags.ide) setup.summary += "; the IDE is the default";
        setup.summary += ")";
    }
    vector<string> leftovers = leftoversOfDroppedTargets(projectPath, flags);
    if (!leftovers.empty()) {
        const bool one = leftovers.size() == 1;
        string names;
        for (size_t i = 0; i < leftovers.size(); ++i) {
            if (i > 0) names += (i + 1 == leftovers.size()) ? " and " : ", ";
            names += leftovers[i];
        }
        setup.leftoverNotice = names + (one ? " is" : " are") + " left in place; "
                               "'trusscli clean --all' removes " + (one ? "it" : "them") +
                               " if not needed.";
    }
    return setup;
}

vector<string> leftoversOfDroppedTargets(const string& projectPath,
                                         const GenerationFlags& flags) {
    const pair<const char*, const optional<bool>*> targets[] = {
        {"web", &flags.web}, {"android", &flags.android}, {"ios", &flags.ios},
    };
    vector<string> out;
    error_code ec;
    for (const auto& [preset, flag] : targets) {
        if (!flag->has_value() || **flag) continue;
        const string dir = ProjectGenerator::buildDirForPreset(preset);
        if (fs::exists(fs::path(projectPath) / dir, ec)) out.push_back(dir + "/");
        for (const string& script : ProjectGenerator::buildScriptsForPreset(preset)) {
            if (fs::exists(fs::path(projectPath) / script, ec)) out.push_back(script);
        }
    }
    return out;
}

string describeGenerationOptions(const ProjectSettings& settings) {
    string out = string("IDE ") + IdeHelper::getIdeId(settings.ideType) + ", targets: native";
    if (settings.generateWebBuild) {
        out += settings.webBackend == 1 ? ", web (WebGL)" : ", web (WebGPU)";
    }
    if (settings.generateAndroidBuild) out += ", android";
#ifdef __APPLE__
    // Written and configured on macOS only (writeCMakePresets())
    if (settings.generateIosBuild) out += ", ios";
#endif
    return out;
}
