#pragma once

// =============================================================================
// tcXml.h - XML read/write
// pugixml wrapper
// =============================================================================

#include <string>
#include <sstream>
#include <fstream>
#include <stdexcept>
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
            // The filesystem path overload preserves native Windows paths.
            std::error_code ec;
            if (fs::file_size(fullPath, ec) > 16u * 1024u * 1024u && !ec)
                logNotice("Xml") << "loading a local document larger than 16 MiB: " << fullPath;
            std::ifstream stream(fullPath, std::ios::binary);
            if (!stream) throw std::runtime_error("could not open XML input");
            std::string input;
            char chunk[8192];
            while (stream) {
                stream.read(chunk, sizeof(chunk));
                input.append(chunk, static_cast<size_t>(stream.gcount()));
            }
            if (stream.bad() || !stream.eof()) throw std::runtime_error("could not read XML input");
            XmlDocument parsed;
            XmlParseResult result = parsed.load_buffer(input.data(), input.size());
            // Use the detected encoding: UTF-16/32 contain ordinary zero bytes.
            // Reject every input NUL, including the final encoded code unit;
            // pugixml's synthetic terminator is not part of this input.
            if (hasFileTerminator(input, result.encoding)) {
                internal::reportImportError("", "XML load error: ", path,
                                            "embedded terminator in XML input");
                return false;
            }
            if (!result) {
                char detail[256];
                std::snprintf(detail, sizeof(detail), "%s (offset: %lld)", result.description(),
                              static_cast<long long>(result.offset));
                internal::reportImportError("", "XML load error: ", path, detail);
                return false;
            }
            if (!checkDepth(parsed)) {
                internal::reportImportError("", "XML load error: ", path,
                                            "XML element nesting exceeds 64");
                return false;
            }
            logVerbose() << "XML loaded: " << fullPath;
            doc_ = std::move(parsed);
            return true;
        } catch (const std::exception& e) {
            internal::reportImportError("Xml", "XML load failed: ", path, e.what());
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
        if (!checkDepth(parsed)) {
            logError("Xml") << "XML element nesting exceeds 64";
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
    static bool hasFileTerminator(const std::string& input, pugi::xml_encoding encoding) {
        size_t width = 1;
        if (encoding == pugi::encoding_utf16_le || encoding == pugi::encoding_utf16_be) width = 2;
        if (encoding == pugi::encoding_utf32_le || encoding == pugi::encoding_utf32_be) width = 4;
        for (size_t offset = 0; offset + width <= input.size(); offset += width) {
            bool zero = true;
            for (size_t byte = 0; byte < width; ++byte)
                if (input[offset + byte] != '\0') zero = false;
            if (zero) return true;
        }
        return false;
    }

    static bool checkDepth(const XmlDocument& parsed) {
        // Walk elements without recursion or additional allocations. The XML
        // parser itself is iterative too; the limit protects DOM consumers.
        auto node = parsed.first_child();
        size_t depth = 0;
        while (node) {
            // Bound (security): nesting must keep DOM consumers' recursion finite; reject the whole document.
            if (node.type() == pugi::node_element && ++depth > 64) return false;
            if (node.first_child()) {
                node = node.first_child();
                continue;
            }
            while (node) {
                if (node.type() == pugi::node_element) --depth;
                if (node.next_sibling()) {
                    node = node.next_sibling();
                    break;
                }
                node = node.parent();
                if (node == parsed) return true;
            }
        }
        return true;
    }

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
