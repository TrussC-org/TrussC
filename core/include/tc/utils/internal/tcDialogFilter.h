#pragma once

#include <string>
#include <vector>

namespace trussc::internal {

inline std::string dialogLowercase(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    }
    return value;
}

inline bool dialogExtensionMatches(const std::string& filename,
                                   const std::vector<std::string>& extensions) {
    if (extensions.empty()) return true;
    const auto lowercase = dialogLowercase(filename);
    for (const auto& extension : extensions) {
        if (lowercase.ends_with("." + dialogLowercase(extension))) return true;
    }
    return false;
}

inline std::string dialogSaveName(const std::string& filename,
                                  const std::vector<std::string>& extensions) {
    return dialogExtensionMatches(filename, extensions) ? filename : filename + "." + extensions.front();
}

inline std::string dialogGtkPattern(const std::string& extension) {
    std::string pattern = "*.";
    for (char character : dialogLowercase(extension)) {
        if (character >= 'a' && character <= 'z') {
            pattern += '[';
            pattern += character;
            pattern += static_cast<char>(character - 'a' + 'A');
            pattern += ']';
        } else {
            pattern += character;
        }
    }
    return pattern;
}

inline std::string dialogWindowsPattern(const std::vector<std::string>& extensions) {
    if (extensions.empty()) return "*.*";
    std::string pattern;
    for (const auto& extension : extensions) {
        if (!pattern.empty()) pattern += ';';
        pattern += "*." + dialogLowercase(extension);
    }
    return pattern;
}

} // namespace trussc::internal
