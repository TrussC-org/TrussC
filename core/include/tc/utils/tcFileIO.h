#pragma once

// =============================================================================
// tcFileIO.h - fs::path <-> C-library boundary helpers (internal)
// =============================================================================
//
// TrussC carries file paths as fs::path end to end. C libraries (stb,
// miniaudio, fopen) only take narrow char* strings, which on Windows are
// interpreted in the active code page — non-ASCII paths get mangled. These
// helpers do the conversion at the last moment, right at the library call:
//
//   - pathToUtf8()  : path -> UTF-8 bytes (for UTF-8-aware sinks such as
//                     stb with STBI(W)_WINDOWS_UTF8, NSString, FFmpeg)
//   - utf8ToPath()  : UTF-8 bytes -> path (for UTF-8 sources such as JSON)
//   - openFile()    : fopen that takes fs::path (wide API on Windows)
//
// Everything here is internal plumbing, not public API.

#include <filesystem>
#include <cstdio>
#include <string>
#include <string_view>

namespace trussc {

namespace fs = std::filesystem;

namespace internal {

// Convert a path to UTF-8 bytes. On POSIX the native encoding already is
// UTF-8; on Windows the native encoding is UTF-16, so go through u8string().
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
