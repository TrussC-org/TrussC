#pragma once
#include "ProjectOps.h"
#include <filesystem>

// Internal tools operations; selection never writes project state or fetches.
namespace projectHub {
struct Snapshot {
    std::string path, name, pin, localRoot, root, commit, error;
    PresetState presets;
    std::vector<std::string> addons, missing, diagnostics;
};
Snapshot inspect(const std::filesystem::path& path, const std::string& fallbackRoot);
projectOps::Result create(const std::filesystem::path& destination,
                          ProjectSettings settings, const projectOps::LogCallback& log = {});
bool usableFramework(const std::filesystem::path& root);
std::filesystem::path documentsDirectory();
std::filesystem::path preferencesPath();
bool defaultIsFramework(const std::filesystem::path& directory);
std::string revealFolder(const std::filesystem::path& directory);
std::string japaneseFont();
}
