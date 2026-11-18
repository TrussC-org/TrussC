#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace share {
namespace fs = std::filesystem;
struct Result { int code; std::string output; };
struct Services {
    std::function<Result(const std::vector<std::string>&)> command;
    std::function<int()> buildWeb;
    std::function<bool(const fs::path& destination, std::string& error)> capture;
    std::function<void(const fs::path&)> view;
};
int run(const fs::path& project, const std::string& name, const std::string& version,
        bool unshare, const Services& services);
bool captureNative(const fs::path& project, const fs::path& executable,
                   const fs::path& destination, std::string& error);
void viewScreenshot(const fs::path& path);
} // namespace share
