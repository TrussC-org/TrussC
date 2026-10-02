#pragma once

// =============================================================================
// tcFile.h - File path utilities, file system operations, and file I/O classes
// =============================================================================

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include "tcLog.h"
#include "tcUtils.h"

namespace trussc {

// =============================================================================
// File Path Utilities
// =============================================================================
// The strings returned here are UTF-8 on every platform (pathToUtf8).
// path::string() would return active-code-page bytes on Windows, and throw
// for characters the code page cannot represent.

// Get filename from path: "dir/test.txt" -> "test.txt"
inline std::string getFileName(const fs::path& path) {
    return pathToUtf8(path.filename());
}

// Get filename without extension: "dir/test.txt" -> "test"
inline std::string getBaseName(const fs::path& path) {
    return pathToUtf8(path.stem());
}

// Get file extension without dot: "dir/test.txt" -> "txt"
inline std::string getFileExtension(const fs::path& path) {
    std::string ext = pathToUtf8(path.extension());
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    return ext;
}

// Get parent directory: "dir/test.txt" -> "dir"
inline std::string getParentDirectory(const fs::path& path) {
    return pathToUtf8(path.parent_path());
}

// Join paths: ("dir", "file.txt") -> "dir/file.txt"
inline std::string joinPath(const fs::path& dir, const fs::path& file) {
    return pathToUtf8(dir / file);
}

// Get absolute path
inline std::string getAbsolutePath(const fs::path& path) {
    return pathToUtf8(std::filesystem::absolute(path));
}

// =============================================================================
// File System Operations
// =============================================================================

// Check if file exists
inline bool fileExists(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    return std::filesystem::exists(fullPath) && std::filesystem::is_regular_file(fullPath);
}

// Check if directory exists
inline bool directoryExists(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    return std::filesystem::exists(fullPath) && std::filesystem::is_directory(fullPath);
}

// Create directory (and parent directories if needed)
// Returns true if directory was created or already exists
inline bool createDirectory(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    try {
        if (std::filesystem::exists(fullPath)) {
            return std::filesystem::is_directory(fullPath);
        }
        return std::filesystem::create_directories(fullPath);
    } catch (const std::exception& e) {
        logError() << "Failed to create directory: " << path << " - " << e.what();
        return false;
    }
}

// List files and directories in a directory
// Returns vector of filenames (not full paths)
inline std::vector<std::string> listDirectory(const fs::path& path) {
    std::vector<std::string> result;
    fs::path fullPath = getDataPath(path);

    if (!std::filesystem::exists(fullPath) || !std::filesystem::is_directory(fullPath)) {
        return result;
    }

    // Error codes instead of exceptions, and one conversion per entry: an
    // entry that fails is logged and skipped, the listing goes on.
    std::error_code ec;
    std::filesystem::directory_iterator it(fullPath, ec);
    std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        try {
            result.push_back(pathToUtf8(it->path().filename()));
        } catch (const std::exception& e) {
            logWarning() << "listDirectory: skipped an entry in " << path << " - " << e.what();
        }
    }
    if (ec) {
        logError() << "Failed to list directory: " << path << " - " << ec.message();
    }

    return result;
}

// Remove file
inline bool removeFile(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    try {
        return std::filesystem::remove(fullPath);
    } catch (const std::exception& e) {
        logError() << "Failed to remove file: " << path << " - " << e.what();
        return false;
    }
}

// Get file size in bytes (-1 on error)
inline int64_t getFileSize(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    try {
        if (!std::filesystem::exists(fullPath)) return -1;
        return static_cast<int64_t>(std::filesystem::file_size(fullPath));
    } catch (const std::exception&) {
        return -1;
    }
}

// =============================================================================
// Simple file read/write functions
// =============================================================================

// Load entire text file into string
// (binary mode: the returned string is the file's exact bytes on every
// platform; Windows text mode would silently fold \r\n into \n)
inline std::string loadTextFile(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        logError() << "Cannot open file: " << path;
        return "";
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return content;
}

// Save string to text file. Relative paths resolve via getDataPath, and a
// missing parent folder is created.
// (binary mode: what you pass is what lands on disk on every platform;
// Windows text mode would expand \n to \r\n, changing the file size)
inline bool saveTextFile(const fs::path& path, const std::string& content) {
    fs::path fullPath = getDataPath(path);
    // "" or "out/": fail before creating any folder
    if (fullPath.filename().empty()) {
        logError() << "No file name in path: " << fullPath;
        return false;
    }
    std::error_code ec;
    fs::path parent = fullPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            logError() << "Cannot create folder: " << parent << " (" << ec.message() << ")";
            return false;
        }
    }
    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        logError() << "Cannot create file: " << path;
        return false;
    }

    file << content;
    file.close();
    if (file.fail()) {
        logError() << "Write error: " << path;
        return false;
    }
    return true;
}

// Append string to text file. Relative paths resolve via getDataPath, and a
// missing parent folder is created.
inline bool appendToFile(const fs::path& path, const std::string& content) {
    fs::path fullPath = getDataPath(path);
    // "" or "out/": fail before creating any folder
    if (fullPath.filename().empty()) {
        logError() << "No file name in path: " << fullPath;
        return false;
    }
    std::error_code ec;
    fs::path parent = fullPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            logError() << "Cannot create folder: " << parent << " (" << ec.message() << ")";
            return false;
        }
    }
    std::ofstream file(fullPath, std::ios::app | std::ios::binary);
    if (!file.is_open()) {
        logError() << "Cannot open file for append: " << path;
        return false;
    }

    file << content;
    file.close();
    if (file.fail()) {
        logError() << "Write error: " << path;
        return false;
    }
    return true;
}

// =============================================================================
// FileWriter - Streaming file writer with immediate flush
// =============================================================================

class FileWriter {
public:
    FileWriter() = default;

    ~FileWriter() {
        close();
    }

    // Non-copyable
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    // Movable
    FileWriter(FileWriter&& other) noexcept : file_(std::move(other.file_)) {}
    FileWriter& operator=(FileWriter&& other) noexcept {
        if (this != &other) {
            close();
            file_ = std::move(other.file_);
        }
        return *this;
    }

    // Open file (append = true to append to existing file). Relative paths
    // resolve via getDataPath, and a missing parent folder is created.
    bool open(const fs::path& path, bool append = false) {
        close();
        fs::path fullPath = getDataPath(path);
        // "" or "out/": fail before creating any folder
        if (fullPath.filename().empty()) {
            logError() << "FileWriter: No file name in path: " << fullPath;
            return false;
        }
        std::error_code ec;
        fs::path parent = fullPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                logError() << "FileWriter: Cannot create folder: " << parent
                           << " (" << ec.message() << ")";
                return false;
            }
        }
        auto mode = std::ios::out | std::ios::binary;
        if (append) mode |= std::ios::app;

        file_.open(fullPath, mode);
        if (!file_.is_open()) {
            logError() << "FileWriter: Cannot open file: " << path;
            return false;
        }
        return true;
    }

    // Close file
    void close() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Check if file is open
    bool isOpen() const {
        return file_.is_open();
    }

    // Write string
    FileWriter& write(const std::string& text) {
        if (file_.is_open()) {
            file_.write(text.data(), text.size());
            file_.flush();
        }
        return *this;
    }

    // Write single character
    FileWriter& write(char c) {
        if (file_.is_open()) {
            file_.put(c);
            file_.flush();
        }
        return *this;
    }

    // Write binary data
    FileWriter& write(const void* data, size_t size) {
        if (file_.is_open()) {
            file_.write(static_cast<const char*>(data), size);
            file_.flush();
        }
        return *this;
    }

    // Write string with newline
    FileWriter& writeLine(const std::string& text = "") {
        write(text);
        write('\n');
        return *this;
    }

    // Explicit flush (already done after each write, but available if needed)
    void flush() {
        if (file_.is_open()) {
            file_.flush();
        }
    }

    // Stream operator for convenience
    template<typename T>
    FileWriter& operator<<(const T& value) {
        if (file_.is_open()) {
            file_ << value;
            file_.flush();
        }
        return *this;
    }

private:
    std::ofstream file_;
};

// =============================================================================
// FileReader - Streaming file reader for large files
// =============================================================================

class FileReader {
public:
    FileReader() = default;

    ~FileReader() {
        close();
    }

    // Non-copyable
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    // Movable
    FileReader(FileReader&& other) noexcept : file_(std::move(other.file_)) {}
    FileReader& operator=(FileReader&& other) noexcept {
        if (this != &other) {
            close();
            file_ = std::move(other.file_);
        }
        return *this;
    }

    // Open file for reading
    bool open(const fs::path& path) {
        close();
        fs::path fullPath = getDataPath(path);
        file_.open(fullPath, std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            logError() << "FileReader: Cannot open file: " << path;
            return false;
        }
        return true;
    }

    // Close file
    void close() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Check if file is open
    bool isOpen() const {
        return file_.is_open();
    }

    // Check if at end of file
    bool eof() const {
        return !file_.is_open() || file_.eof();
    }

    // Read single line (returns empty string at EOF)
    std::string readLine() {
        std::string line;
        if (file_.is_open() && std::getline(file_, line)) {
            // Remove \r if present (for Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
        }
        return line;
    }

    // Read line into provided string (returns false at EOF)
    bool readLine(std::string& line) {
        if (!file_.is_open()) return false;

        if (std::getline(file_, line)) {
            // Remove \r if present (for Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        return false;
    }

    // Read single character (-1 at EOF)
    int readChar() {
        if (!file_.is_open()) return -1;
        return file_.get();
    }

    // Read binary data (returns bytes actually read)
    size_t read(void* buffer, size_t size) {
        if (!file_.is_open()) return 0;
        file_.read(static_cast<char*>(buffer), size);
        return static_cast<size_t>(file_.gcount());
    }

    // Seek to position
    void seek(size_t pos) {
        if (file_.is_open()) {
            file_.seekg(pos);
        }
    }

    // Get current position
    size_t tell() {
        if (!file_.is_open()) return 0;
        return static_cast<size_t>(file_.tellg());
    }

    // Get remaining bytes
    size_t remaining() {
        if (!file_.is_open()) return 0;
        auto current = file_.tellg();
        file_.seekg(0, std::ios::end);
        auto end = file_.tellg();
        file_.seekg(current);
        return static_cast<size_t>(end - current);
    }

private:
    std::ifstream file_;
};

} // namespace trussc
