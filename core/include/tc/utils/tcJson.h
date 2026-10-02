#pragma once

// =============================================================================
// tcJson.h - JSON read/write
// nlohmann/json wrapper
// =============================================================================

#include <fstream>
#include <string>
#include "nlohmann/json.hpp"
#include "tcLog.h"
#include "tcUtils.h"

namespace trussc {

// Type alias to use nlohmann::json directly
using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// JSON file loading
// Relative paths are resolved via getDataPath (like oF)
// ---------------------------------------------------------------------------
inline Json loadJson(const fs::path& path) {
    fs::path fullPath = getDataPath(path);
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        logError() << "Cannot open JSON file: " << path;
        return Json();
    }

    try {
        Json j = Json::parse(file);
        logVerbose() << "JSON loaded: " << fullPath;
        return j;
    } catch (const Json::parse_error& e) {
        logError() << "JSON parse error: " << path << " - " << e.what();
        return Json();
    }
}

// ---------------------------------------------------------------------------
// JSON file writing
// Relative paths are resolved via getDataPath (like oF), and a missing
// parent folder is created
// ---------------------------------------------------------------------------
inline bool saveJson(const Json& j, const fs::path& path, int indent = 2) {
    fs::path fullPath = getDataPath(path);
    // "" or "out/": fail before creating any folder
    if (fullPath.filename().empty()) {
        logError() << "No file name in JSON file path: " << fullPath;
        return false;
    }
    // Serialize before touching the disk: a serialization error (e.g. a
    // string that is not valid UTF-8) leaves an existing file untouched.
    std::string text;
    try {
        text = (indent >= 0) ? j.dump(indent) : j.dump();  // < 0: compact
    } catch (const std::exception& e) {
        logError() << "JSON serialize error: " << path << " - " << e.what();
        return false;
    }

    std::error_code ec;
    fs::path parent = fullPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            logError() << "Cannot create folder for JSON file: " << parent
                       << " (" << ec.message() << ")";
            return false;
        }
    }
    // Binary mode, like saveTextFile: the same bytes on every platform (LF)
    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        logError() << "Cannot create JSON file: " << path;
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (file.fail()) {
        logError() << "JSON write error: " << path;
        return false;
    }
    logVerbose() << "JSON saved: " << fullPath;
    return true;
}

// ---------------------------------------------------------------------------
// Parse JSON from string
// ---------------------------------------------------------------------------
inline Json parseJson(const std::string& str) {
    try {
        return Json::parse(str);
    } catch (const Json::parse_error& e) {
        logError() << "JSON parse error: " << e.what();
        return Json();
    }
}

// ---------------------------------------------------------------------------
// Convert JSON to string
// ---------------------------------------------------------------------------
inline std::string toJsonString(const Json& j, int indent = 2) {
    if (indent >= 0) {
        return j.dump(indent);
    }
    return j.dump();
}

} // namespace trussc
