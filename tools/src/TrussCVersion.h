#pragma once

#include <filesystem>
#include <string>

namespace trusscVersion {
struct Spec {
    std::string owner = "TrussC-org";
    std::string repo = "TrussC";
    std::string ref;
    bool commit = false;
};
Spec parse(const std::string& text);
std::string localPath(const std::filesystem::path& project);
void setLocalPath(const std::filesystem::path& project, const std::string& path);
// Empty means no selection: the caller retains the existing autodetection.
// Fetching is permitted only for an explicit update (including use).
std::string resolve(const std::filesystem::path& project, bool fetch);
// Shared cache acquisition for TrussC and external addons. spec must come from parse().
std::string acquire(const Spec& spec, bool fetch, bool addon = false,
                    const std::string& sourceUrl = "");
std::string readBounded(const std::filesystem::path& path, size_t limit);
std::string commitAt(const std::filesystem::path& root);
}
