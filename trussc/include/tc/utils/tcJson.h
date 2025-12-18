#pragma once

// =============================================================================
// tcJson.h - JSON 読み書き
// nlohmann/json のラッパー
// =============================================================================

#include <fstream>
#include <string>
#include "nlohmann/json.hpp"
#include "tcLog.h"

namespace trussc {

// nlohmann::json をそのまま使えるようにエイリアス
using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// JSON ファイル読み込み
// ---------------------------------------------------------------------------
inline Json loadJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        tcLogError() << "JSON ファイルを開けない: " << path;
        return Json();
    }

    try {
        Json j = Json::parse(file);
        tcLogVerbose() << "JSON 読み込み完了: " << path;
        return j;
    } catch (const Json::parse_error& e) {
        tcLogError() << "JSON パースエラー: " << path << " - " << e.what();
        return Json();
    }
}

// ---------------------------------------------------------------------------
// JSON ファイル書き込み
// ---------------------------------------------------------------------------
inline bool saveJson(const Json& j, const std::string& path, int indent = 2) {
    std::ofstream file(path);
    if (!file.is_open()) {
        tcLogError() << "JSON ファイルを作成できない: " << path;
        return false;
    }

    try {
        if (indent >= 0) {
            file << j.dump(indent);
        } else {
            file << j.dump();  // 圧縮形式
        }
        tcLogVerbose() << "JSON 書き込み完了: " << path;
        return true;
    } catch (const std::exception& e) {
        tcLogError() << "JSON 書き込みエラー: " << path << " - " << e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// 文字列から JSON をパース
// ---------------------------------------------------------------------------
inline Json parseJson(const std::string& str) {
    try {
        return Json::parse(str);
    } catch (const Json::parse_error& e) {
        tcLogError() << "JSON パースエラー: " << e.what();
        return Json();
    }
}

// ---------------------------------------------------------------------------
// JSON を文字列に変換
// ---------------------------------------------------------------------------
inline std::string toJsonString(const Json& j, int indent = 2) {
    if (indent >= 0) {
        return j.dump(indent);
    }
    return j.dump();
}

} // namespace trussc
