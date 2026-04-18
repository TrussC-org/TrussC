#pragma once

// =============================================================================
// tcXml.h - XML read/write
// pugixml wrapper
// =============================================================================

#include <string>
#include <sstream>
#include "pugixml/pugixml.hpp"
#include "tcLog.h"

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

    // Load from file
    bool load(const std::string& path) {
        XmlParseResult result = doc_.load_file(path.c_str());
        if (!result) {
            logError() << "XML load error: " << path
                         << " - " << result.description()
                         << " (offset: " << result.offset << ")";
            return false;
        }
        logVerbose() << "XML loaded: " << path;
        return true;
    }

    // Load from string
    bool parse(const std::string& str) {
        XmlParseResult result = doc_.load_string(str.c_str());
        if (!result) {
            logError() << "XML parse error: " << result.description()
                         << " (offset: " << result.offset << ")";
            return false;
        }
        return true;
    }

    // Save to file
    bool save(const std::string& path, const std::string& indent = "  ") const {
        bool success = doc_.save_file(path.c_str(), indent.c_str());
        if (!success) {
            logError() << "XML write error: " << path;
            return false;
        }
        logVerbose() << "XML saved: " << path;
        return true;
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
inline Xml loadXml(const std::string& path) {
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
