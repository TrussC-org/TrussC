#pragma once

#include "../../utils/internal/tcBoundedInput.h"
#include "../../utils/tcLog.h"
#include <filesystem>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>
#include <cstdint>

namespace trussc::internal {

struct HotReloadScanLoss {
    // Owner lifetime: one watcher (source) or host process (cache). Queries do not reset totals.
    uint64_t skippedEntries = 0, incompleteScans = 0, rejectedCaches = 0;
    bool warned = false;
    void warn() {
        if (warned) return;
        warned = true;
#if defined(__cpp_exceptions)
        try {
#endif
            logWarning("HotReload") << "Skipped source/cache input: scan limits 65536 entries, depth 64, "
                                      "16 MiB names/cache, 128 KiB path, 1 MiB cache line; failures counted";
#if defined(__cpp_exceptions)
        } catch (...) {}
#endif
    }
};

inline std::vector<std::filesystem::path> hotReloadSources(
        const std::filesystem::path& root, HotReloadScanLoss& loss) {
    namespace fs = std::filesystem;
    // Bound (security): source trees are external input; finite work and retained paths protect the host. Overflow skips entries/subtrees or ends the scan and is counted.
    constexpr size_t entryLimit = 65536, depthLimit = 64, nameBytes = 16 * 1024 * 1024,
                     pathBytes = 128 * 1024;
    std::vector<fs::path> paths;
#if defined(__cpp_exceptions)
    try {
#endif
    std::error_code error;
    fs::recursive_directory_iterator cursor(root, fs::directory_options::none, error), end;
    if (error) { ++loss.incompleteScans; loss.warn(); return paths; }
    size_t entries = 0, names = 0;
    while (cursor != end) {
        if (entries == entryLimit) { ++loss.incompleteScans; loss.warn(); break; }
        ++entries;
        const auto& path = cursor->path();
        const size_t units = path.native().size();
        bool skip = units > pathBytes / sizeof(fs::path::value_type);
        const bool directory = cursor->is_directory(error);
        if (error) { error.clear(); skip = true; }
        if (directory && static_cast<size_t>(cursor.depth()) + 1 >= depthLimit) skip = true;
        if (skip) {
            cursor.disable_recursion_pending();
            ++loss.skippedEntries;
            loss.warn();
        } else if (cursor->is_regular_file(error)) {
            auto ext = path.extension().native();
            for (auto& character : ext) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
            if (ext == fs::path(".cpp").native() || ext == fs::path(".h").native() ||
                ext == fs::path(".hpp").native() || ext == fs::path(".mm").native()) {
                const size_t bytes = (units + 1) * sizeof(fs::path::value_type);
                if (bytes > nameBytes - names) { ++loss.skippedEntries; loss.warn(); }
                else { paths.push_back(path); names += bytes; }
            }
        } else if (error) { ++loss.skippedEntries; loss.warn(); error.clear(); }
        cursor.increment(error);
        if (error) { ++loss.incompleteScans; loss.warn(); break; }
    }
    return paths;
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        ++loss.incompleteScans; loss.warn(); return paths;
    }
#endif
}

HotReloadScanLoss& hotReloadCacheLoss();

inline std::string hotReloadCMakeCommand(const std::filesystem::path& cachePath,
                                       HotReloadScanLoss& loss) {
    // Bound (security): build-tree cache text is external input, not an app document. Overflow rejects this cache/line and is counted.
    constexpr size_t fileBytes = 16 * 1024 * 1024, lineBytes = 1024 * 1024;
#if defined(__cpp_exceptions)
    try {
#endif
    std::error_code error;
    const auto size = std::filesystem::file_size(cachePath, error);
    if (error) {
        // A missing cache uses normal tool discovery; malformed/unreadable
        // existing entries are observed failures, not silent fallbacks.
        if (error != std::errc::no_such_file_or_directory) {
            ++loss.rejectedCaches;
            loss.warn();
        }
        return {};
    }
    if (size > fileBytes) { ++loss.rejectedCaches; loss.warn(); return {}; }
#ifdef _WIN32
    auto* opened = _wfopen(cachePath.c_str(), L"rb");
#else
    auto* opened = std::fopen(cachePath.c_str(), "rb");
#endif
    std::unique_ptr<FILE, decltype(&std::fclose)> file(opened, &std::fclose);
    if (!file) { ++loss.rejectedCaches; loss.warn(); return {}; }
    // FILE preserves the I/O error indicator even on libraries whose filebuf
    // turns a failed fread into ordinary EOF (including libc++).
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
    std::string command;
    bool rejected = false;
    readBoundedLines(input, lineBytes, fileBytes, [&](std::string_view line) {
        constexpr std::string_view key = "CMAKE_COMMAND:INTERNAL=";
        if (line.substr(0, key.size()) == key) {
            line.remove_prefix(key.size());
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            command.assign(line);
        }
    }, [&](bool) { rejected = true; });
    // A growing cache cannot publish a command from a prefix that passed stat.
    if (rejected || input.bad() || std::ferror(file.get())) { ++loss.rejectedCaches; loss.warn(); return {}; }
    return command;
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        ++loss.rejectedCaches; loss.warn(); return {};
    }
#endif
}

} // namespace trussc::internal
