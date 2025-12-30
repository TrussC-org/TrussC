#include "VsDetector.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
namespace fs = std::filesystem;

vector<VsVersionInfo> VsDetector::detectInstalledVersions() {
    vector<VsVersionInfo> versions;

#ifdef _WIN32
    // Use vswhere to detect installed Visual Studio versions with JSON output
    string vswhereCmd = R"("C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -all -format json)";

    FILE* pipe = _popen(vswhereCmd.c_str(), "r");
    if (pipe) {
        string jsonOutput;
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            jsonOutput += buffer;
        }
        _pclose(pipe);

        // Simple JSON parsing for installationPath and installationVersion
        size_t pos = 0;
        while ((pos = jsonOutput.find("\"installationPath\"", pos)) != string::npos) {
            // Find installationPath value
            size_t colonPos = jsonOutput.find(':', pos);
            size_t quoteStart = jsonOutput.find('"', colonPos + 1);
            size_t quoteEnd = jsonOutput.find('"', quoteStart + 1);
            if (quoteStart == string::npos || quoteEnd == string::npos) break;

            string installPath = jsonOutput.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            // Unescape backslashes (JSON uses \ for single backslash)
            size_t bsPos;
            while ((bsPos = installPath.find("\\\\")) != string::npos) {
                installPath.replace(bsPos, 2, "\\");
            }

            // Find installationVersion in the same object (search forwards from current position)
            size_t versionPos = jsonOutput.find("\"installationVersion\"", pos);

            if (versionPos != string::npos && versionPos < pos + 1000) {
                size_t vColonPos = jsonOutput.find(':', versionPos);
                size_t vQuoteStart = jsonOutput.find('"', vColonPos + 1);
                size_t vQuoteEnd = jsonOutput.find('"', vQuoteStart + 1);

                if (vQuoteStart != string::npos && vQuoteEnd != string::npos) {
                    string versionStr = jsonOutput.substr(vQuoteStart + 1, vQuoteEnd - vQuoteStart - 1);

                    // Parse major version
                    int majorVersion = 0;
                    try {
                        majorVersion = stoi(versionStr.substr(0, versionStr.find('.')));
                    } catch (...) {
                        pos = quoteEnd;
                        continue;
                    }

                    // Check if already added
                    bool found = false;
                    for (const auto& v : versions) {
                        if (v.version == majorVersion) {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        pos = quoteEnd;
                        continue;
                    }

                    // Create version info
                    VsVersionInfo info;
                    info.version = majorVersion;

                    if (majorVersion == 17) {
                        info.displayName = "Visual Studio 2022";
                        info.generator = "Visual Studio 17 2022";
                    } else if (majorVersion == 18) {
                        info.displayName = "Visual Studio 2026";
                        info.generator = "Visual Studio 18 2026";
                    } else if (majorVersion == 16) {
                        info.displayName = "Visual Studio 2019";
                        info.generator = "Visual Studio 16 2019";
                    } else {
                        pos = quoteEnd;
                        continue;
                    }

                    // Construct paths from VS installation
                    info.cmakePath = installPath + R"(\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe)";
                    info.vcvarsallPath = installPath + R"(\VC\Auxiliary\Build\vcvarsall.bat)";

                    // Verify cmake and vcvarsall exist
                    if (fs::exists(info.cmakePath) && fs::exists(info.vcvarsallPath)) {
                        versions.push_back(info);
                    }
                }
            }
            pos = quoteEnd;
        }
    }

    // Sort by version (newest first)
    sort(versions.begin(), versions.end(),
         [](const VsVersionInfo& a, const VsVersionInfo& b) {
             return a.version > b.version;
         });

    // If nothing found, add VS2022 as fallback
    if (versions.empty()) {
        VsVersionInfo fallback;
        fallback.version = 17;
        fallback.displayName = "Visual Studio 2022";
        fallback.generator = "Visual Studio 17 2022";
        fallback.cmakePath = "";  // Will use PATH
        versions.push_back(fallback);
    }
#else
    // Not Windows - add dummy entry (won't be used)
    VsVersionInfo dummy;
    dummy.version = 17;
    dummy.displayName = "Visual Studio 2022";
    dummy.generator = "Visual Studio 17 2022";
    dummy.cmakePath = "";
    versions.push_back(dummy);
#endif

    return versions;
}

tuple<int, int, int> VsDetector::getCmakeVersion() {
    int major = 0, minor = 0, patch = 0;

    FILE* pipe = nullptr;
#ifdef _WIN32
    pipe = _popen("cmake --version", "r");
#else
    pipe = popen("cmake --version", "r");
#endif

    if (pipe) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            // Parse "cmake version X.Y.Z"
            string line(buffer);
            size_t pos = line.find("version ");
            if (pos != string::npos) {
                string ver = line.substr(pos + 8);
                sscanf(ver.c_str(), "%d.%d.%d", &major, &minor, &patch);
            }
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }

    return {major, minor, patch};
}

bool VsDetector::checkCmakeVersionForVs(int vsVersion, string& errorMessage) {
    if (vsVersion < 18) {
        // VS2022 and earlier work with CMake 3.x
        return true;
    }

    // VS2026 (version 18) requires CMake 4.2+
    auto [major, minor, patch] = getCmakeVersion();

    if (major > 4) return true;
    if (major == 4 && minor >= 2) return true;

    // Build error message
    errorMessage = "Visual Studio 2026 requires CMake 4.2 or later.\n";
    errorMessage += "Current CMake version in PATH: ";
    if (major == 0) {
        errorMessage += "not found\n";
    } else {
        errorMessage += to_string(major) + "." + to_string(minor) + "." + to_string(patch) + "\n";
    }
    errorMessage += "\nPlease update CMake or ensure the correct version is in your PATH.";

    return false;
}
