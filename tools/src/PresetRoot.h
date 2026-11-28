#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

// Internal tools helpers shared by build and read-only Hub inspection.
namespace projectOps {
#ifdef __APPLE__
static constexpr const char* kNativePreset = "macos";
#elif defined(_WIN32)
static constexpr const char* kNativePreset = "windows";
#elif defined(__linux__)
static constexpr const char* kNativePreset = "linux";
#else
static constexpr const char* kNativePreset = nullptr;
#endif

// Resolve the concrete preset core without changing project metadata.
inline std::string presetAddonRoot(const std::string& projectPath, const std::string& targetPreset,
                                   std::string root, const nlohmann::json& data) {
    if (root.empty() && data.is_object() && data.contains("configurePresets")) {
        for (const auto& preset : data["configurePresets"]) {
            if (preset.value("name", "") != targetPreset || !preset.contains("cacheVariables")) continue;
            auto core = preset["cacheVariables"].value("TRUSSC_DIR", nlohmann::json());
            if (core.is_object()) core = core.value("value", nlohmann::json());
            if (!core.is_string()) continue;
            std::string path = core.get<std::string>();
            const std::string macro = "${sourceDir}";
            if (path.compare(0, macro.size(), macro) == 0) path.replace(0, macro.size(), std::filesystem::absolute(projectPath).generic_string());
            if (path.find('$') != std::string::npos) continue;
            std::filesystem::path corePath(path);
            if (corePath.is_relative()) corePath = std::filesystem::path(projectPath) / corePath;
            root = std::filesystem::absolute(corePath).lexically_normal().parent_path().string();
        }
    }
    return root;
}

}
