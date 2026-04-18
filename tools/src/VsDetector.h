#pragma once

#include <string>
#include <vector>
#include <tuple>

// Visual Studio version information
struct VsVersionInfo {
    int version;            // 17 = VS2022, 18 = VS2026
    std::string displayName; // "Visual Studio 2022", "Visual Studio 2026"
    std::string generator;   // "Visual Studio 17 2022", "Visual Studio 18 2026"
    std::string installPath; // VS installation path
    std::string cmakePath;   // Path to cmake bundled with this VS version
    std::string vcvarsallPath; // Path to vcvarsall.bat for setting up environment
    std::string ninjaPath;   // Path to ninja bundled with this VS version
    std::string vcToolsVersion;   // VC Tools version (e.g., "14.50.35717")
    std::string windowsSdkVersion; // Windows SDK version (e.g., "10.0.26100.0")
};

// Visual Studio detection utility (Windows only)
class VsDetector {
public:
    // Detect installed Visual Studio versions
    static std::vector<VsVersionInfo> detectInstalledVersions();

    // Get CMake version (major, minor, patch)
    static std::tuple<int, int, int> getCmakeVersion();

    // Check if CMake version is compatible with VS version
    // Returns true if compatible, false otherwise
    // Sets errorMessage if incompatible
    static bool checkCmakeVersionForVs(int vsVersion, std::string& errorMessage);
};
