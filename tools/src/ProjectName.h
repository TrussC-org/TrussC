#pragma once

#include <initializer_list>
#include <string>

// CMP0037 permits ASCII letters, digits, '_', '.', '+', '-'. Replace each
// other character with one '_': a whole UTF-8 sequence counts as one character
// (日本語 -> ___), and an invalid byte counts as one. Locale-independent; valid
// names stay unchanged, including leading digits.
inline std::string sanitizeProjectName(const std::string& input) {
    std::string name;
    for (size_t i = 0; i < input.size();) {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '.' || ch == '+' || ch == '-';
        if (allowed) { name += char(ch); ++i; continue; }
        name += '_';
        // Length of a well-formed UTF-8 sequence; anything else is one byte.
        size_t length = ch >= 0xF0 && ch <= 0xF4 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC2 && ch <= 0xDF ? 2 : 1;
        if (ch >= 0xF5 || (ch >= 0x80 && ch < 0xC2)) length = 1;
        size_t next = i + 1;
        while (next < i + length && next < input.size() &&
               (static_cast<unsigned char>(input[next]) & 0xC0) == 0x80) ++next;
        i = next == i + length ? next : i + 1;
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
