#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace trusscVersion {
struct Spec {
    std::string owner = "TrussC-org";
    std::string repo = "TrussC";
    std::string ref;
    bool commit = false;
};
std::filesystem::path delegateBinary(const std::filesystem::path& root);
bool supportsDelegation(const std::filesystem::path& root);
std::string tryReleaseDelegate(const std::filesystem::path& root, const Spec& tag);
Spec parse(const std::string& text);
std::string localPath(const std::filesystem::path& project);
void setLocalPath(const std::filesystem::path& project, const std::string& path);
// Empty means no selection: the caller retains the existing autodetection.
// Fetching is permitted only for an explicit update (including use).
// releaseTag is populated only when fetching resolves an explicit tag.
std::string resolve(const std::filesystem::path& project, bool fetch, Spec* releaseTag = nullptr);
// Shared cache acquisition for TrussC and external addons. spec must come from parse().
std::string acquire(const Spec& spec, bool fetch, bool addon = false,
                    const std::string& sourceUrl = "", Spec* releaseTag = nullptr);
std::string readBounded(const std::filesystem::path& path, size_t limit);
// Records usage only for acquired cache paths; local folders are ignored.
void markCachedUsed(const std::filesystem::path& path);
// Cache commands share acquisition paths and containment checks.
int cacheCommand(const std::vector<std::string>& args);
// Removes one acquired version and prunes its worktree registration; keeps the bare repo.
void removeCachedVersion(const std::filesystem::path& path, const Spec& spec);
std::string commitAt(const std::filesystem::path& root);
}
