#pragma once

// =============================================================================
// tcJson.h - JSON read/write
// nlohmann/json wrapper
// =============================================================================

#include <fstream>
#include <string>
#include "nlohmann/json.hpp"
#include "tcLog.h"

namespace trussc {

// Type alias to use nlohmann::json directly
using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// JSON file loading
// ---------------------------------------------------------------------------
inline Json loadJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        logError() << "Cannot open JSON file: " << path;
        return Json();
    }

    try {
        Json j = Json::parse(file);
        logVerbose() << "JSON loaded: " << path;
        return j;
    } catch (const Json::parse_error& e) {
        logError() << "JSON parse error: " << path << " - " << e.what();
        return Json();
    }
}

// ---------------------------------------------------------------------------
// JSON file writing
// ---------------------------------------------------------------------------
inline bool saveJson(const Json& j, const std::string& path, int indent = 2) {
    std::ofstream file(path);
    if (!file.is_open()) {
        logError() << "Cannot create JSON file: " << path;
        return false;
    }

    try {
        if (indent >= 0) {
            file << j.dump(indent);
        } else {
            file << j.dump();  // Compact format
        }
        logVerbose() << "JSON saved: " << path;
        return true;
    } catch (const std::exception& e) {
        logError() << "JSON write error: " << path << " - " << e.what();
        return false;
    }
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
