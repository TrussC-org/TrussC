#pragma once

// =============================================================================
// tcFileIO.h - fs::path <-> UTF-8 conversion and C-library boundary helpers
// =============================================================================
//
// TrussC carries file paths as fs::path end to end. C libraries (stb,
// miniaudio, fopen) only take narrow char* strings, which on Windows are
// interpreted in the active code page — non-ASCII paths get mangled. These
// helpers do the conversion at the last moment, right at the library call:
//
//   - pathToUtf8()  : path -> UTF-8 bytes (for UTF-8-aware sinks such as
//                     stb with STBI(W)_WINDOWS_UTF8, NSString, FFmpeg, and
//                     for text: Font, JSON)
//   - utf8ToPath()  : UTF-8 bytes -> path (for UTF-8 sources such as JSON)
//   - pathToDisplayUtf8() : path -> UTF-8 text for logs and error messages;
//                     never fails on the name (see below)
//   - openFile()    : fopen that takes fs::path (wide API on Windows)
//
// pathToUtf8() / utf8ToPath() are public API: they convert the same way on
// every platform and never go through the Windows code page, unlike
// path::string() and fs::path(std::string). pathToDisplayUtf8() and
// openFile() are internal plumbing.

#include <filesystem>
#include <cstdio>
#include <string>
#include <string_view>

namespace trussc {

namespace fs = std::filesystem;

// Resolve a relative path against the data folder; absolute paths pass
// through. Defined in tcUtils.h (included by TrussC.h); declared here so the
// loaders in headers that tcUtils.h itself includes (Sound, Pixels) can call it.
inline fs::path getDataPath(const fs::path& filename);

// Convert a path to UTF-8 bytes. On POSIX the native encoding already is
// UTF-8; on Windows the native encoding is UTF-16, so go through u8string().
// Exact or nothing: on Windows it throws for a name that is not valid UTF-16
// (an unpaired surrogate, which NTFS allows), since no UTF-8 string would
// name that file again. Log text uses internal::pathToDisplayUtf8() instead.
inline std::string pathToUtf8(const fs::path& p) {
#ifdef _WIN32
    auto u8 = p.u8string();   // std::u8string (char8_t) in C++20
    return std::string(u8.begin(), u8.end());
#else
    return p.string();
#endif
}

// Convert UTF-8 bytes to a path. fs::path(std::string) would interpret the
// bytes in the active code page on Windows — this never does.
inline fs::path utf8ToPath(std::string_view utf8) {
#ifdef _WIN32
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return fs::path(std::string(utf8));
#endif
}

namespace internal {

// These two lived here before they became public API; keep the qualified
// internal:: spelling working for existing callers (core, addons).
using trussc::pathToUtf8;
using trussc::utf8ToPath;

// UTF-16 -> UTF-8 that never fails: an unpaired surrogate becomes U+FFFD.
// CharT holds UTF-16 code units (wchar_t on Windows, char16_t anywhere).
template <class CharT>
inline std::string utf16ToUtf8Lossy(std::basic_string_view<CharT> in) {
    static_assert(sizeof(CharT) == 2, "utf16ToUtf8Lossy takes UTF-16 code units");
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char32_t c = static_cast<char16_t>(in[i]);
        if (c >= 0xD800 && c <= 0xDFFF) {
            const char32_t next = i + 1 < in.size() ? static_cast<char16_t>(in[i + 1]) : 0;
            if (c <= 0xDBFF && next >= 0xDC00 && next <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00);
                ++i;
            } else {
                c = 0xFFFD;   // a high surrogate with no low one after it, or a lone low one
            }
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// pathToUtf8() for text people read: log lines and error messages. It never
// throws for the name: where pathToUtf8() throws (on Windows, a name holding
// an unpaired UTF-16 surrogate), the bad unit comes out as U+FFFD. Not for a
// string that goes back into a path: with a replacement in it, it names a
// different file. On POSIX both return the native bytes unchanged.
inline std::string pathToDisplayUtf8(const fs::path& p) {
#ifdef _WIN32
    return utf16ToUtf8Lossy(std::wstring_view(p.native()));
#else
    return p.string();
#endif
}

// fopen that accepts fs::path. Uses _wfopen on Windows so non-ASCII paths
// survive; mode strings are short ASCII ("rb", "wb", ...).
inline std::FILE* openFile(const fs::path& p, const char* mode) {
#ifdef _WIN32
    wchar_t wmode[8];
    size_t i = 0;
    for (; mode[i] != '\0' && i < 7; ++i) wmode[i] = static_cast<wchar_t>(mode[i]);
    wmode[i] = L'\0';
    return _wfopen(p.c_str(), wmode);
#else
    return std::fopen(p.c_str(), mode);
#endif
}

} // namespace internal

} // namespace trussc
