#pragma once
#include "tc/utils/tcLog.h"
#include <cstdio>

namespace trussc::internal {
inline void appendImportField(std::string& message, const char* field) { message += field; }
inline void appendImportField(std::string& message, const fs::path& field) {
    message += pathToDisplayUtf8(field);
}
inline void printImportField(const char* field) noexcept { std::fputs(field, stderr); }
inline void printImportField(const fs::path& field) noexcept {
#ifdef _WIN32
    // Match pathToDisplayUtf8 without allocating, even under persistent OOM.
    const auto& units = field.native();
    for (size_t i = 0; i < units.size(); ++i) {
        char32_t c = static_cast<char16_t>(units[i]);
        if (c >= 0xD800 && c <= 0xDFFF) {
            const char32_t next = i + 1 < units.size() ? static_cast<char16_t>(units[i + 1]) : 0;
            if (c <= 0xDBFF && next >= 0xDC00 && next <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + next - 0xDC00;
                ++i;
            } else c = 0xFFFD;
        }
        if (c < 0x80) std::fputc(static_cast<int>(c), stderr);
        else {
            if (c >= 0x10000) std::fputc(0xF0 | (c >> 18), stderr);
            if (c >= 0x800) {
                std::fputc(c < 0x10000 ? 0xE0 | (c >> 12) : 0x80 | ((c >> 12) & 0x3F), stderr);
                std::fputc(0x80 | ((c >> 6) & 0x3F), stderr);
            } else std::fputc(0xC0 | (c >> 6), stderr);
            std::fputc(0x80 | (c & 0x3F), stderr);
        }
    }
#else
    std::fputs(field.c_str(), stderr);
#endif
}
// Optional file context for implementations shared by file and memory imports.
inline void appendImportField(std::string& message, const fs::path* field) {
    if (field) { message += ": "; appendImportField(message, *field); }
}
inline void printImportField(const fs::path* field) noexcept {
    if (field) { std::fputs(": ", stderr); printImportField(*field); }
}
// Recovery must not use LogStream: its noexcept destructor can allocate.
// Take fields by reference: conversion/copying and detail formatting belong
// inside this guard, never at a call site that promises clean OOM recovery.
template <class Field = const char*>
inline void reportImportError(const char* module, const char* reason,
                              const Field& field = "", const char* detail = nullptr) noexcept {
    try {
        std::string message;
        if (*module) message = std::string("[") + module + "] ";
        message += reason;
        appendImportField(message, field);
        if (detail) { message += " - "; message += detail; }
        getLogger().log(LogLevel::Error, message);
    } catch (...) {
        // C stdio reports its own failures without throwing C++ exceptions.
        std::fputs("[ERROR] ", stderr);
        if (*module) std::fprintf(stderr, "[%s] ", module);
        std::fputs(reason, stderr);
        printImportField(field);
        if (detail) std::fprintf(stderr, " - %s", detail);
        std::fputc('\n', stderr);
    }
}
}
