#pragma once

// =============================================================================
// tcXml.h - XML 読み書き
// pugixml のラッパー
// =============================================================================

#include <string>
#include <sstream>
#include "pugixml/pugixml.hpp"
#include "tcLog.h"

namespace trussc {

// pugixml の型をエイリアス
using XmlDocument = pugi::xml_document;
using XmlNode = pugi::xml_node;
using XmlAttribute = pugi::xml_attribute;
using XmlParseResult = pugi::xml_parse_result;

// ---------------------------------------------------------------------------
// Xml クラス - XML ドキュメントのラッパー
// ---------------------------------------------------------------------------
class Xml {
public:
    Xml() = default;

    // ファイルから読み込み
    bool load(const std::string& path) {
        XmlParseResult result = doc_.load_file(path.c_str());
        if (!result) {
            tcLogError() << "XML 読み込みエラー: " << path
                         << " - " << result.description()
                         << " (offset: " << result.offset << ")";
            return false;
        }
        tcLogVerbose() << "XML 読み込み完了: " << path;
        return true;
    }

    // 文字列から読み込み
    bool parse(const std::string& str) {
        XmlParseResult result = doc_.load_string(str.c_str());
        if (!result) {
            tcLogError() << "XML パースエラー: " << result.description()
                         << " (offset: " << result.offset << ")";
            return false;
        }
        return true;
    }

    // ファイルに保存
    bool save(const std::string& path, const std::string& indent = "  ") const {
        bool success = doc_.save_file(path.c_str(), indent.c_str());
        if (!success) {
            tcLogError() << "XML 書き込みエラー: " << path;
            return false;
        }
        tcLogVerbose() << "XML 書き込み完了: " << path;
        return true;
    }

    // 文字列に変換
    std::string toString(const std::string& indent = "  ") const {
        std::ostringstream oss;
        doc_.save(oss, indent.c_str());
        return oss.str();
    }

    // ルートノードを取得
    XmlNode root() {
        return doc_.document_element();
    }

    // ルートノードを取得（const）
    XmlNode root() const {
        return doc_.document_element();
    }

    // 新しいルートノードを追加
    XmlNode addRoot(const std::string& name) {
        return doc_.append_child(name.c_str());
    }

    // 子ノードを名前で検索
    XmlNode child(const std::string& name) {
        return doc_.child(name.c_str());
    }

    // 内部ドキュメントへのアクセス
    XmlDocument& document() { return doc_; }
    const XmlDocument& document() const { return doc_; }

    // 空かどうか
    bool empty() const { return doc_.empty(); }

    // XML 宣言を追加
    void addDeclaration(const std::string& version = "1.0",
                        const std::string& encoding = "UTF-8") {
        auto decl = doc_.prepend_child(pugi::node_declaration);
        decl.append_attribute("version") = version.c_str();
        decl.append_attribute("encoding") = encoding.c_str();
    }

private:
    XmlDocument doc_;
};

// ---------------------------------------------------------------------------
// 便利関数
// ---------------------------------------------------------------------------

// ファイルから XML を読み込み
inline Xml loadXml(const std::string& path) {
    Xml xml;
    xml.load(path);
    return xml;
}

// 文字列から XML をパース
inline Xml parseXml(const std::string& str) {
    Xml xml;
    xml.parse(str);
    return xml;
}

} // namespace trussc
