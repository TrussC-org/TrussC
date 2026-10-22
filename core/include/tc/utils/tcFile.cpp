#include "tcFile.h"
#include <new>
#include <stdexcept>

namespace trussc {
namespace {
constexpr size_t largeReadNoticeBytes = 256u * 1024u * 1024u;
// Avoid LogStream's noexcept destructor on an allocation-failure path.
void fileLog(LogLevel level, const char* message, const fs::path* path = nullptr,
             const char* detail = nullptr) noexcept {
    try {
        std::string text(message);
        if (path) text += internal::pathToDisplayUtf8(*path);
        if (detail) { text += " - "; text += detail; }
        getLogger().log(level, text);
    } catch (...) {
        std::fputs(level == LogLevel::Notice ? "[NOTICE] " : // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
                   level == LogLevel::Warning ? "[WARNING] " : "[ERROR] ", stderr);
        std::fputs(message, stderr); // log-check: allow (allocation-free fallback after Logger/diagnostic allocation fails)
        std::fputc('\n', stderr);
    }
}
}

std::string loadTextFile(const fs::path& path) {
    try {
        const auto fullPath = internal::resolveLoadPath(path);
        if (!internal::isLoadFile(fullPath)) {
            fileLog(LogLevel::Error, "[File] file not found: ", &fullPath);
            return {};
        }
        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            fileLog(LogLevel::Error, "[File] file not found: ", &fullPath);
            return {};
        }
        std::string content;
        char chunk[8 * 1024];
        bool noticed = false;
        while (file.read(chunk, sizeof(chunk)) || file.gcount()) {
            // Grow only from bytes actually read, including files that grow
            // while open. std::string append has amortized linear growth.
            content.append(chunk, static_cast<size_t>(file.gcount()));
            if (!noticed && content.size() >= largeReadNoticeBytes) {
                fileLog(LogLevel::Notice, "[File] Reading a large local text file (at least 256 MiB)");
                noticed = true;
            }
        }
        if (file.bad() || (file.fail() && !file.eof())) {
            fileLog(LogLevel::Error, "[File] Cannot read complete text file");
            return {};
        }
        return content;
    } catch (const std::exception&) {
        fileLog(LogLevel::Error, "[File] Cannot read complete text file (I/O or allocation failure)");
        return {};
    }
}

std::vector<std::string> listDirectory(const fs::path& path) {
    try {
        std::vector<std::string> result;
        const auto fullPath = getDataPath(path);
        std::error_code ec;
        const auto status = fs::status(fullPath, ec);
        if (!fs::is_directory(status)) {
            if (status.type() != fs::file_type::not_found && ec)
                fileLog(LogLevel::Error, "[File] Cannot inspect directory: ", &fullPath, ec.message().c_str());
            return {};
        }
        fs::directory_iterator it(fullPath, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            // Keep only the names required by the return value; no snapshot of
            // directory_entry objects or file metadata is retained.
            std::string name;
            try {
                name = pathToUtf8(it->path().filename());
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::length_error&) {
                throw;
            } catch (const std::exception& e) {
                fileLog(LogLevel::Warning, "listDirectory: skipped an entry in ", &path, e.what());
                continue;
            }
            result.push_back(std::move(name));
        }
        if (!ec) return result;
        fileLog(LogLevel::Error, "Failed to list directory: ", &fullPath, ec.message().c_str());
    } catch (const std::exception&) {
        fileLog(LogLevel::Error, "[File] Cannot read complete directory listing (I/O or allocation failure)");
    }
    return {};
}

std::string FileReader::readLine() {
    std::string line;
    readLine(line);
    return line;
}

bool FileReader::readLine(std::string& line) {
    line.clear();
    if (!file_.is_open()) {
        std::string().swap(line);
        return false;
    }
    try {
        // getline grows geometrically, so a long line uses linear storage and
        // amortized linear copying. Local lines have no size policy cap.
        if (std::getline(file_, line) && !file_.bad()) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() >= largeReadNoticeBytes)
                fileLog(LogLevel::Notice, "[FileReader] Read a large local line (at least 256 MiB)");
            return true;
        }
    } catch (const std::exception&) {
        file_.setstate(std::ios::badbit);
    }
    std::string().swap(line);
    if (file_.bad() || (file_.fail() && !file_.eof()))
        fileLog(LogLevel::Error, "[FileReader] Cannot read complete line (I/O or allocation failure)");
    return false;
}
} // namespace trussc
