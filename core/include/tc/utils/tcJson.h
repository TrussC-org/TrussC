#pragma once

// =============================================================================
// tcJson.h - JSON read/write
// nlohmann/json wrapper
// =============================================================================

#include <fstream>
#include "impl/tcDocumentInput.h"
#include "impl/tcImportError.h"
#include <string>
#include "nlohmann/json.hpp"
#include "tcLog.h"
#include "tcLoadResult.h"
#include "tcUtils.h"

namespace trussc {

// Type alias to use nlohmann::json directly
using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// JSON file loading
// Relative paths are resolved via getDataPath (like oF)
// ---------------------------------------------------------------------------
inline Json loadJson(const fs::path& path) {
    try {
        fs::path fullPath = internal::resolveLoadPath(path);
        if (!internal::checkLoadFile(fullPath, "Json")) return Json();
        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            internal::reportMissingLoad(fullPath, "Json");
            return Json();
        }
        std::error_code ec;
        if (fs::file_size(fullPath, ec) > 16u * 1024u * 1024u && !ec)
            logNotice("Json") << "loading a local document larger than 16 MiB: " << fullPath;
        std::string bytes;
        const char* readError = nullptr;
        if (!internal::readDocumentBytes(file, bytes, &readError)) {
            internal::reportImportError("Json", "failed to read the complete JSON document: ", path, readError);
            return Json();
        }
        // The bundled lexer treats raw NUL as EOF; reject it before parsing
        // so a valid prefix cannot hide an unread tail. Escaped NUL is valid.
        if (bytes.find('\0') != std::string::npos) {
            internal::reportImportError("", "JSON parse error: ", path, "invalid JSON document: raw NUL");
            return Json();
        }
        Json j = Json::parse(bytes, nullptr, false);
        if (j.is_discarded()) { internal::reportImportError("", "JSON parse error: ", path, "invalid JSON document"); return Json(); }
        logVerbose() << "JSON loaded: " << fullPath;
        return j;
    } catch (const std::exception& e) {
        internal::reportImportError("Json", "JSON load failed: ", path, e.what());
        return Json();
    }
}

namespace internal {
// Parse into a temporary so every failure preserves the caller's value.
inline LoadResult parseJsonDocument(const std::string& bytes, Json& out) {
    const auto nulPosition = bytes.find('\0');
    if (nulPosition != std::string::npos)
        return LoadResult::fail(LoadError::DecodeFailed,
            "invalid JSON document: raw NUL at byte " + std::to_string(nulPosition + 1));
    try {
        Json parsed = Json::parse(bytes);
        out.swap(parsed);
        return LoadResult::success();
    } catch (const Json::parse_error& e) {
        return LoadResult::fail(LoadError::DecodeFailed,
            "invalid JSON at byte " + std::to_string(e.byte) + ": invalid syntax");
    } catch (const Json::out_of_range&) {
        // The decoder reports numeric overflow (406) without a byte position.
        return LoadResult::fail(LoadError::DecodeFailed, "invalid JSON: number out of range");
    }
}
} // namespace internal

// Reports success separately from the value, including a valid JSON null.
// The one-argument overload retains its original value and logging contract.
TC_LUA_SKIP inline LoadResult loadJson(const fs::path& path, Json& out) {
    try {
        const fs::path fullPath = internal::resolveLoadPath(path);
        auto checked = internal::checkLoadFile(fullPath, "Json");
        if (!checked) return checked;
        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) return internal::reportMissingLoad(fullPath, "Json");
        std::error_code ec;
        if (fs::file_size(fullPath, ec) > 16u * 1024u * 1024u && !ec)
            logNotice("Json") << "loading a local document larger than 16 MiB: " << fullPath;
        std::string bytes;
        const char* readError = nullptr;
        if (!internal::readDocumentBytes(file, bytes, &readError)) {
            const auto message = "failed to read the complete JSON document: " +
                internal::pathToDisplayUtf8(path) + (readError ? " - " + std::string(readError) : "");
            internal::reportImportError("Json", message.c_str());
            return LoadResult::fail(LoadError::DecodeFailed, message);
        }
        Json parsed;
        auto result = internal::parseJsonDocument(bytes, parsed);
        if (!result) {
            result.message = internal::pathToDisplayUtf8(path) + " - " + result.message;
            internal::reportImportError("Json", result.message.c_str());
            return result;
        }
        logVerbose() << "JSON loaded: " << fullPath;
        out.swap(parsed);
        return result;
    } catch (const std::exception&) {
        const auto message = "JSON load failed: " + internal::pathToDisplayUtf8(path);
        internal::reportImportError("Json", message.c_str());
        return LoadResult::fail(LoadError::Unknown, message);
    }
}

// ---------------------------------------------------------------------------
// JSON file writing
// Relative paths are resolved via getDataPath (like oF), and a missing
// parent folder is created. A path inside the app bundle is refused (use
// getUserDataPath()).
// ---------------------------------------------------------------------------
inline SaveResult saveJson(const Json& j, const fs::path& path, int indent = 2) {
    std::string text;
    try {
        text = (indent >= 0) ? j.dump(indent) : j.dump();
    } catch (const std::exception& e) {
        return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed,
            "JSON serialize error: " + internal::pathToDisplayUtf8(path) + " - " + e.what()), "Json");
    }
    fs::path fullPath;
    auto prepared = internal::prepareOutputPath(path, fullPath, "Json");
    if (!prepared) return prepared;
    return internal::atomicWrite(fullPath, text.data(), text.size(), "Json");
}

// ---------------------------------------------------------------------------
// Parse JSON from string
// ---------------------------------------------------------------------------
inline Json parseJson(const std::string& str) {
    try {
        if (str.size() > 16u * 1024u * 1024u)
            logNotice("Json") << "parsing a document larger than 16 MiB";
        if (str.find('\0') != std::string::npos) {
            internal::reportImportError("Json", "invalid JSON document: raw NUL");
            return Json();
        }
        Json parsed = Json::parse(str, nullptr, false);
        if (parsed.is_discarded()) { internal::reportImportError("Json", "invalid JSON document"); return Json(); }
        return parsed;
    } catch (const std::exception& e) {
        internal::reportImportError("Json", "JSON parse failed: ", e.what());
        return Json();
    }
}

// A failed parse leaves out unchanged; available byte positions are one-based.
TC_LUA_SKIP inline LoadResult parseJson(const std::string& str, Json& out) {
    try {
        if (str.size() > 16u * 1024u * 1024u)
            logNotice("Json") << "parsing a document larger than 16 MiB";
        Json parsed;
        auto result = internal::parseJsonDocument(str, parsed);
        if (!result) internal::reportImportError("Json", result.message.c_str());
        else out.swap(parsed);
        return result;
    } catch (const std::exception&) {
        const std::string message = "JSON parse failed";
        internal::reportImportError("Json", message.c_str());
        return LoadResult::fail(LoadError::Unknown, message);
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
