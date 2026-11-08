#pragma once

#include "tc/utils/tcFileIO.h"
#include <istream>
#include <vector>

namespace trussc::internal {

// /proc/self/cmdline is NUL-separated; spaces and UTF-8 bytes are data.
inline std::vector<std::string> parseProcCommandLine(std::istream& input) {
    std::vector<std::string> args;
    std::string arg;
    // Bound (representation): strings and the argument vector use their native
    // max_size limits. As with filesDropped, only OS and available-memory limits apply.
    while (std::getline(input, arg, '\0')) args.push_back(std::move(arg));
    // A failed read gives an empty list, never a prefix of the arguments.
    if (input.bad()) return {};
    return args;
}

// Fake argv seam shared by the Windows and Linux launch paths.
inline std::vector<fs::path> filesFromCommandLine(const std::vector<std::string>& args) {
    std::vector<fs::path> files;
    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        // Leading '-' always means a flag. Use ./-name to open such a file.
        if (arg.empty() || arg.front() == '-') continue;
        auto path = utf8ToPath(arg);
        std::error_code error;
        const auto status = fs::status(path, error);
        if (error || !fs::exists(status)) continue;
        // Capture the launch working directory before setup() can change it.
        path = fs::absolute(path, error);
        if (!error) files.push_back(std::move(path));
    }
    return files;
}

} // namespace trussc::internal
