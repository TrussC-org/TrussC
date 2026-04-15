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

// Get filename from path: "dir/test.txt" -> "test.txt"
inline std::string getFileName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

// Get filename without extension: "dir/test.txt" -> "test"
inline std::string getBaseName(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// Get file extension without dot: "dir/test.txt" -> "txt"
inline std::string getFileExtension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    return ext;
}

// Get parent directory: "dir/test.txt" -> "dir"
inline std::string getParentDirectory(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

// Join paths: ("dir", "file.txt") -> "dir/file.txt"
inline std::string joinPath(const std::string& dir, const std::string& file) {
    return (std::filesystem::path(dir) / file).string();
}

// Get absolute path
inline std::string getAbsolutePath(const std::string& path) {
    return std::filesystem::absolute(path).string();
}

// =============================================================================
// File System Operations
// =============================================================================

// Check if file exists
inline bool fileExists(const std::string& path) {
    std::string fullPath = getDataPath(path);
    return std::filesystem::exists(fullPath) && std::filesystem::is_regular_file(fullPath);
}

// Check if directory exists
inline bool directoryExists(const std::string& path) {
    std::string fullPath = getDataPath(path);
    return std::filesystem::exists(fullPath) && std::filesystem::is_directory(fullPath);
}

// Create directory (and parent directories if needed)
// Returns true if directory was created or already exists
inline bool createDirectory(const std::string& path) {
    std::string fullPath = getDataPath(path);
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
inline std::vector<std::string> listDirectory(const std::string& path) {
    std::vector<std::string> result;
    std::string fullPath = getDataPath(path);

    if (!std::filesystem::exists(fullPath) || !std::filesystem::is_directory(fullPath)) {
        return result;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(fullPath)) {
            result.push_back(entry.path().filename().string());
        }
    } catch (const std::exception& e) {
        logError() << "Failed to list directory: " << path << " - " << e.what();
    }

    return result;
}

// Remove file
inline bool removeFile(const std::string& path) {
    std::string fullPath = getDataPath(path);
    try {
        return std::filesystem::remove(fullPath);
    } catch (const std::exception& e) {
        logError() << "Failed to remove file: " << path << " - " << e.what();
        return false;
    }
}

// Get file size in bytes (-1 on error)
inline int64_t getFileSize(const std::string& path) {
    std::string fullPath = getDataPath(path);
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
inline std::string loadTextFile(const std::string& path) {
    std::string fullPath = getDataPath(path);
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        logError() << "Cannot open file: " << path;
        return "";
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return content;
}

// Save string to text file
inline bool saveTextFile(const std::string& path, const std::string& content) {
    std::string fullPath = getDataPath(path);
    std::ofstream file(fullPath);
    if (!file.is_open()) {
        logError() << "Cannot create file: " << path;
        return false;
    }

    file << content;
    return true;
}

// Append string to text file
inline bool appendToFile(const std::string& path, const std::string& content) {
    std::string fullPath = getDataPath(path);
    std::ofstream file(fullPath, std::ios::app);
    if (!file.is_open()) {
        logError() << "Cannot open file for append: " << path;
        return false;
    }

    file << content;
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

    // Open file (append = true to append to existing file)
    bool open(const std::string& path, bool append = false) {
        close();
        std::string fullPath = getDataPath(path);
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
    bool open(const std::string& path) {
        close();
        std::string fullPath = getDataPath(path);
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
