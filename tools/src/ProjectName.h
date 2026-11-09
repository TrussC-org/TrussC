#pragma once

#include <initializer_list>
#include <string>

// CMP0037 permits ASCII letters, digits, '_', '.', '+', '-'. Replace each
// other byte with '_' (including each byte of a UTF-8 name), independent of
// the process locale. Keep valid names unchanged, including leading digits.
inline std::string sanitizeProjectName(std::string name) {
    for (char& ch : name) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '.' || ch == '+' || ch == '-';
        if (!allowed) ch = '_';
    }
    if (name.empty()) return "app";

    // CMake's built-in targets (including optional CTest/CPack targets).
    for (const char* reserved : {"all", "clean", "help", "install", "test",
             "package", "package_source", "edit_cache", "rebuild_cache",
             "preinstall", "ALL_BUILD", "ZERO_CHECK", "INSTALL"}) {
        if (name == reserved) return "app_" + name;
    }

    // Windows device names remain reserved with an extension, e.g. CON.exe.
    std::string device = name.substr(0, name.find('.'));
    for (char& ch : device) {
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    }
    if (device == "CON" || device == "PRN" || device == "AUX" || device == "NUL" ||
        (device.size() == 4 && (device.compare(0, 3, "COM") == 0 ||
                               device.compare(0, 3, "LPT") == 0) &&
         device[3] >= '1' && device[3] <= '9')) {
        return "app_" + name;
    }
    return name;
}
