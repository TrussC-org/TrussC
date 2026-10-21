#pragma once

// =============================================================================
// tcXml.h - XML read/write
// pugixml wrapper
// =============================================================================

#include <string>
#include <sstream>
#include "pugixml/pugixml.hpp"
#include "tcLog.h"
#include "impl/tcImportError.h"
#include "tcUtils.h"   // getDataPath

namespace trussc {

// Type aliases for pugixml
using XmlDocument = pugi::xml_document;
using XmlNode = pugi::xml_node;
using XmlAttribute = pugi::xml_attribute;
using XmlParseResult = pugi::xml_parse_result;

// ---------------------------------------------------------------------------
// Xml class - XML document wrapper
// ---------------------------------------------------------------------------
class Xml {
public:
    Xml() = default;

    // Load from file (relative paths resolved via getDataPath, like loadJson)
    bool load(const fs::path& path) {
        try {
            fs::path fullPath = internal::resolveLoadPath(path);
            if (!internal::checkLoadFile(fullPath, "Xml")) return false;
            // fullPath.c_str() is wchar_t* on Windows — pugixml has a wide
            // load_file overload there, so non-ASCII paths survive.
            std::error_code ec;
            if (fs::file_size(fullPath, ec) > 16u * 1024u * 1024u && !ec)
                logNotice("Xml") << "loading a local document larger than 16 MiB: " << fullPath;
            XmlDocument parsed;
            XmlParseResult result = parsed.load_file(fullPath.c_str());
            if (!result) {
                logError() << "XML load error: " << path
                             << " - " << result.description()
                             << " (offset: " << result.offset << ")";
                return false;
            }
            logVerbose() << "XML loaded: " << fullPath;
            doc_ = std::move(parsed);
            return true;
        } catch (const std::exception& e) {
            internal::reportImportError("Xml", "XML load failed: ", e.what());
            return false;
        }
    }

    // Load from string
    bool parse(const std::string& str) {
        if (str.find('\0') != std::string::npos) {
            logError("Xml") << "embedded terminator in XML input";
            return false;
        }
        if (str.size() > 16u * 1024u * 1024u)
            logNotice("Xml") << "parsing a document larger than 16 MiB";
        XmlDocument parsed;
        XmlParseResult result = parsed.load_buffer(str.data(), str.size());
        if (!result) {
            logError() << "XML parse error: " << result.description()
                         << " (offset: " << result.offset << ")";
            return false;
        }
        doc_ = std::move(parsed);
        return true;
    }

    // Save to file (relative paths resolved via getDataPath, like saveJson;
    // a missing parent folder is created; a path inside the app bundle is
    // refused, use getUserDataPath())
    SaveResult save(const fs::path& path, const std::string& indent = "  ") const {
        std::string text;
        try {
            std::ostringstream stream;
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            doc_.save(stream, indent.c_str());
            text = stream.str();
        } catch (const std::exception& e) {
            return internal::reportSaveFailure(SaveResult::fail(SaveError::WriteFailed,
                "XML serialize error: " + internal::pathToDisplayUtf8(path) + " - " + e.what()), "Xml");
        }
        fs::path fullPath;
        auto prepared = internal::prepareOutputPath(path, fullPath, "Xml");
        if (!prepared) return prepared;
        return internal::atomicWrite(fullPath, text.data(), text.size(), "Xml");
    }

    // Convert to string
    std::string toString(const std::string& indent = "  ") const {
        std::ostringstream oss;
        doc_.save(oss, indent.c_str());
        return oss.str();
    }

    // Get root node
    XmlNode root() {
        return doc_.document_element();
    }

    // Get root node (const)
    XmlNode root() const {
        return doc_.document_element();
    }

    // Add new root node
    XmlNode addRoot(const std::string& name) {
        return doc_.append_child(name.c_str());
    }

    // Find child node by name
    XmlNode child(const std::string& name) {
        return doc_.child(name.c_str());
    }

    // Access to internal document
    XmlDocument& document() { return doc_; }
    const XmlDocument& document() const { return doc_; }

    // Check if empty
    bool empty() const { return doc_.empty(); }

    // Add XML declaration
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
// Utility functions
// ---------------------------------------------------------------------------

// Load XML from file
inline Xml loadXml(const fs::path& path) {
    Xml xml;
    xml.load(path);
    return xml;
}

// Parse XML from string
inline Xml parseXml(const std::string& str) {
    Xml xml;
    xml.parse(str);
    return xml;
}

} // namespace trussc
