#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <bitset>
#include <cstdint>

// Forward declaration for tcPlatform.h (avoid circular include)
namespace trussc { namespace platform {
    std::string getExecutableDir();
}}

namespace trussc {

// ---------------------------------------------------------------------------
// Data Path (similar to oF's ofToDataPath)
// ---------------------------------------------------------------------------

namespace internal {
    // Default is "data/" (relative to executable directory)
    inline std::string dataPathRoot = "data/";
    inline bool dataPathRootIsAbsolute = false;
}

// Set the data path root
// If relative path, resolved relative to executable directory
// If absolute path (starts with /), used as-is
inline void setDataPathRoot(const std::string& path) {
    internal::dataPathRoot = path;
    // Add trailing slash if missing
    if (!internal::dataPathRoot.empty() && internal::dataPathRoot.back() != '/') {
        internal::dataPathRoot += '/';
    }
    // Record whether path is absolute
    internal::dataPathRootIsAbsolute = (!path.empty() && path[0] == '/');
}

// Get the data path root
inline std::string getDataPathRoot() {
    return internal::dataPathRoot;
}

// Get data path for a filename
// Resolved relative to executable directory
inline std::string getDataPath(const std::string& filename) {
    if (internal::dataPathRootIsAbsolute) {
        // Absolute path: use as-is
        return internal::dataPathRoot + filename;
    } else {
        // Relative path: resolve relative to executable directory
        return platform::getExecutableDir() + internal::dataPathRoot + filename;
    }
}

// For macOS bundle distribution: Set data path to Resources folder
// Will reference xxx.app/Contents/Resources/data/
// No-op on non-macOS platforms
inline void setDataPathToResources() {
    #ifdef __APPLE__
    setDataPathRoot("../Resources/data/");
    #endif
}

// ---------------------------------------------------------------------------
// toString - Convert value to string
// ---------------------------------------------------------------------------

// Basic string conversion
template <class T>
std::string toString(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// Convert to string with specified decimal precision
// Example: toString(3.14159, 2) → "3.14"
template <class T>
std::string toString(const T& value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// Convert to string with specified width and fill character
// Example: toString(42, 5, '0') → "00042"
template <class T>
std::string toString(const T& value, int width, char fill) {
    std::ostringstream out;
    out << std::setfill(fill) << std::setw(width) << value;
    return out.str();
}

// Convert to string with precision, width, and fill character
// Example: toString(3.14, 2, 6, '0') → "003.14"
template <class T>
std::string toString(const T& value, int precision, int width, char fill) {
    std::ostringstream out;
    out << std::fixed << std::setfill(fill) << std::setw(width) << std::setprecision(precision) << value;
    return out.str();
}

// Convert vector to string
// Example: toString(vector{1,2,3}) → "{1, 2, 3}"
template <class T>
std::string toString(const std::vector<T>& values) {
    std::ostringstream out;
    out << "{";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out << ", ";
        out << values[i];
    }
    out << "}";
    return out.str();
}

// ---------------------------------------------------------------------------
// String to Number Conversion
// ---------------------------------------------------------------------------

// Convert string to integer
inline int toInt(const std::string& str) {
    try {
        return std::stoi(str);
    } catch (...) {
        return 0;
    }
}

// Convert string to 64-bit integer
inline int64_t toInt64(const std::string& str) {
    try {
        return std::stoll(str);
    } catch (...) {
        return 0;
    }
}

// Convert string to float
inline float toFloat(const std::string& str) {
    try {
        return std::stof(str);
    } catch (...) {
        return 0.0f;
    }
}

// Convert string to double
inline double toDouble(const std::string& str) {
    try {
        return std::stod(str);
    } catch (...) {
        return 0.0;
    }
}

// Convert string to bool ("true", "TRUE", "1" → true)
inline bool toBool(const std::string& str) {
    if (str.empty()) return false;
    // Convert to lowercase and compare
    std::string lower = str;
    for (auto& c : lower) c = std::tolower(c);
    return (lower == "true" || lower == "1" || lower == "yes");
}

// ---------------------------------------------------------------------------
// toHex - Convert to hexadecimal string
// ---------------------------------------------------------------------------

// Convert number to hexadecimal string
template <class T>
std::string toHex(const T& value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

// Convert integer to hexadecimal string (zero-padded)
inline std::string toHex(int value, int width = 0) {
    std::ostringstream out;
    if (width > 0) {
        out << std::setfill('0') << std::setw(width);
    }
    out << std::hex << std::uppercase << value;
    return out.str();
}

// Convert string to hexadecimal string
inline std::string toHex(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        out << std::setfill('0') << std::setw(2) << std::hex << std::uppercase << (int)c;
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// toBinary - Convert to binary string
// ---------------------------------------------------------------------------

// Convert integer to binary string
inline std::string toBinary(int value) {
    return std::bitset<32>(value).to_string();
}

inline std::string toBinary(unsigned int value) {
    return std::bitset<32>(value).to_string();
}

inline std::string toBinary(char value) {
    return std::bitset<8>(value).to_string();
}

inline std::string toBinary(unsigned char value) {
    return std::bitset<8>(value).to_string();
}

// Convert string to binary string (each character as 8 bits)
inline std::string toBinary(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if (!result.empty()) result += " ";
        result += std::bitset<8>(c).to_string();
    }
    return result;
}

// ---------------------------------------------------------------------------
// fromHex - Convert hexadecimal string to number
// ---------------------------------------------------------------------------

inline int hexToInt(const std::string& hexStr) {
    try {
        return std::stoi(hexStr, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

inline unsigned int hexToUInt(const std::string& hexStr) {
    try {
        return static_cast<unsigned int>(std::stoul(hexStr, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// String Operations
// ---------------------------------------------------------------------------

/// Check if string contains another string
/// Same as oF's ofIsStringInString
inline bool isStringInString(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Count occurrences of needle in haystack
/// Same as oF's ofStringTimesInString
inline std::size_t stringTimesInString(const std::string& haystack, const std::string& needle) {
    const size_t step = needle.size();
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        pos += step;
        ++count;
    }
    return count;
}

/// Split string by delimiter
/// Same argument order as oF's ofSplitString
/// @param source Source string
/// @param delimiter Delimiter string
/// @param ignoreEmpty Whether to ignore empty elements
/// @param trim Whether to trim whitespace from each element
inline std::vector<std::string> splitString(const std::string& source, const std::string& delimiter, bool ignoreEmpty = false, bool trim = false) {
    std::vector<std::string> result;
    if (delimiter.empty()) {
        result.push_back(source);
        return result;
    }
    std::string::const_iterator substart = source.begin(), subend;
    while (true) {
        subend = std::search(substart, source.end(), delimiter.begin(), delimiter.end());
        std::string sub(substart, subend);
        if (trim) {
            // Remove leading/trailing whitespace
            size_t start = sub.find_first_not_of(" \t\r\n");
            size_t end = sub.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                sub = sub.substr(start, end - start + 1);
            } else if (start == std::string::npos) {
                sub.clear();
            }
        }
        if (!ignoreEmpty || !sub.empty()) {
            result.push_back(sub);
        }
        if (subend == source.end()) {
            break;
        }
        substart = subend + delimiter.size();
    }
    return result;
}

/// Join string array with delimiter
/// Same as oF's ofJoinString
inline std::string joinString(const std::vector<std::string>& stringElements, const std::string& delimiter) {
    std::string str;
    if (stringElements.empty()) {
        return str;
    }
    auto numStrings = stringElements.size();
    std::string::size_type strSize = delimiter.size() * (numStrings - 1);
    for (const std::string& s : stringElements) {
        strSize += s.size();
    }
    str.reserve(strSize);
    str += stringElements[0];
    for (decltype(numStrings) i = 1; i < numStrings; ++i) {
        str += delimiter;
        str += stringElements[i];
    }
    return str;
}

/// Replace occurrences in string (in-place)
/// Same as oF's ofStringReplace
inline void stringReplace(std::string& input, const std::string& searchStr, const std::string& replaceStr) {
    auto pos = input.find(searchStr);
    while (pos != std::string::npos) {
        input.replace(pos, searchStr.size(), replaceStr);
        pos += replaceStr.size();
        std::string nextfind(input.begin() + pos, input.end());
        auto nextpos = nextfind.find(searchStr);
        if (nextpos == std::string::npos) {
            break;
        }
        pos += nextpos;
    }
}

/// Remove leading/trailing whitespace from string
/// Same as oF's ofTrim
inline std::string trim(const std::string& src) {
    size_t start = src.find_first_not_of(" \t\r\n");
    size_t end = src.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    return src.substr(start, end - start + 1);
}

/// Remove leading whitespace from string
/// Same as oF's ofTrimFront
inline std::string trimFront(const std::string& src) {
    size_t start = src.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    return src.substr(start);
}

/// Remove trailing whitespace from string
/// Same as oF's ofTrimBack
inline std::string trimBack(const std::string& src) {
    size_t end = src.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) {
        return "";
    }
    return src.substr(0, end + 1);
}

/// Convert string to lowercase
/// Same as oF's ofToLower
inline std::string toLower(const std::string& src) {
    std::string dst = src;
    for (auto& c : dst) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return dst;
}

/// Convert string to uppercase
/// Same as oF's ofToUpper
inline std::string toUpper(const std::string& src) {
    std::string dst = src;
    for (auto& c : dst) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return dst;
}

} // namespace trussc
