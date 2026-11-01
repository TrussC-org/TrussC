#pragma once

#include "../../utils/tcLog.h"
#include <filesystem>
#include <cstdio>
#include <memory>
#include <istream>
#include <string_view>
#include <string>
#include <vector>
#include <new>

namespace trussc::internal {

inline std::vector<std::filesystem::path> hotReloadSources(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
#if defined(__cpp_exceptions)
    try {
#endif
        std::error_code error;
        fs::recursive_directory_iterator cursor(root, fs::directory_options::none, error), end;
        while (!error && cursor != end) {
            if (cursor->is_regular_file(error)) {
                auto ext = cursor->path().extension().native();
                for (auto& c : ext) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (ext == fs::path(".cpp").native() || ext == fs::path(".h").native() ||
                    ext == fs::path(".hpp").native() || ext == fs::path(".mm").native() ||
                    ext == fs::path(".glsl").native())
                    paths.push_back(cursor->path());
            }
            if (error) {
                cursor.disable_recursion_pending();
                error.clear();
            }
            cursor.increment(error);
        }
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        try { logError("HotReload") << "allocation failed"; } catch (...) {}
        return {};
    }
#endif
    return paths;
}

inline std::string hotReloadCMakeCommand(const std::filesystem::path& cachePath) {
#if defined(__cpp_exceptions)
    try {
#endif
#ifdef _WIN32
        auto* opened = _wfopen(cachePath.c_str(), L"rb");
#else
        auto* opened = std::fopen(cachePath.c_str(), "rb");
#endif
        std::unique_ptr<FILE, decltype(&std::fclose)> file(opened, &std::fclose);
        if (!file) return {};
        class FileBuffer : public std::streambuf {
        public:
            explicit FileBuffer(FILE* file) : file_(file) {}
        protected:
            int_type underflow() override {
                const size_t count = std::fread(bytes_, 1, sizeof(bytes_), file_);
                if (!count) return traits_type::eof();
                setg(bytes_, bytes_, bytes_ + count);
                return traits_type::to_int_type(*gptr());
            }
        private:
            FILE* file_;
            char bytes_[4096];
        } buffer(file.get());
        std::istream input(&buffer);
        std::string line, command;
        constexpr std::string_view key = "CMAKE_COMMAND:INTERNAL=";
        while (std::getline(input, line)) {
            if (line.compare(0, key.size(), key) == 0) {
                command = line.substr(key.size());
                if (!command.empty() && command.back() == '\r') command.pop_back();
            }
        }
        return input.bad() || std::ferror(file.get()) ? std::string{} : command;
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        try { logError("HotReload") << "allocation failed"; } catch (...) {}
        return {};
    }
#endif
}

} // namespace trussc::internal
