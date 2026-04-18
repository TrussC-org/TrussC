#pragma once

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// IDE selection types
enum class IdeType {
    CMakeOnly,
    VSCode,
    Cursor,
    Xcode,
    VisualStudio
};

// IDE integration helper
class IdeHelper {
public:
    // Open project in specified IDE
    // Returns empty string on success, error message on failure
    static std::string openInIde(IdeType ideType, const std::string& path);

    // Generate Xcode Debug/Release schemes
    static void generateXcodeSchemes(const std::string& path);

    // Get IDE display name
    static const char* getIdeName(IdeType type);
};
