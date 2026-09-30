#include "ProjectState.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;
using Json = nlohmann::json;

PresetState parsePresetState(const string& jsonText) {
    PresetState state;
    Json data = Json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (!data.is_object()) return state;
    state.found = true;

    auto presets = data.find("configurePresets");
    if (presets != data.end() && presets->is_array()) {
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
                    // Written as "WGPU" or "GLES3" (see writeCMakePresets)
                    if (backend != vars->end() && backend->is_string() &&
                        backend->get<string>() == "GLES3") {
                        state.webBackend = 1;
                    }
                }
            }
            else if (n == "android") state.android = true;
            else if (n == "ios")     state.ios = true;
        }
    }

    auto vendor = data.find("vendor");
    if (vendor != data.end() && vendor->is_object()) {
        auto trussc = vendor->find("trussc");
        if (trussc != vendor->end() && trussc->is_object()) {
            auto ide = trussc->find("ide");
            if (ide != trussc->end() && ide->is_string()) {
                state.hasIde = IdeHelper::parseIdeId(ide->get<string>(), state.ide);
            }
        }
    }
    return state;
}

PresetState readPresetState(const string& projectPath) {
    fs::path path = fs::path(projectPath) / "CMakePresets.json";
    error_code ec;
    if (!fs::is_regular_file(path, ec)) return PresetState();
    ifstream file(path, ios::binary);
    if (!file) return PresetState();
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
    }
    if (flags.web) settings.generateWebBuild = *flags.web;
    if (flags.android) settings.generateAndroidBuild = *flags.android;
    if (flags.ios) settings.generateIosBuild = *flags.ios;
    if (flags.ide) settings.ideType = *flags.ide;
}

string describeGenerationOptions(const ProjectSettings& settings) {
    string out = string("IDE ") + IdeHelper::getIdeId(settings.ideType) + ", targets: native";
    if (settings.generateWebBuild) {
        out += settings.webBackend == 1 ? ", web (WebGL)" : ", web (WebGPU)";
    }
    if (settings.generateAndroidBuild) out += ", android";
    if (settings.generateIosBuild) out += ", ios";
    return out;
}
