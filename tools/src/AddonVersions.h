#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace addonVersions {
struct Spec { std::string owner, name, ref; };
Spec parse(const std::string& text);
struct Source { std::string owner, url; };
using Lookup = std::function<Source(const Spec&, bool)>;
struct Paths : std::map<std::string, std::string> {
    std::string order; // CMake list, parents before dependencies.
};
Paths resolve(const std::filesystem::path& project, const std::filesystem::path& trussc,
              bool fetch, const Lookup& lookup, const std::vector<std::string>* selection = nullptr);
void setLocal(const std::filesystem::path& project, const std::string& name, const std::string& path);
void saveResolved(const std::filesystem::path& project, const Paths& paths);
}
