#pragma once
#include "tc/utils/tcAnnotations.h"
#include "tc/utils/tcFileIO.h"   // fs alias

// =============================================================================
// System font lookup — resolve a font name (PostScript name or family/display
// name, depending on platform) to a file path that can be passed to Font::load.
// Also enumerate the installed fonts the OS knows about.
//
// Backends:
//   - macOS : CoreText (CTFontDescriptor)
//   - Linux : fontconfig
//   - Win   : DirectWrite
//   - Web   : no-op (return empty; Web font loading is URL-based)
//   - iOS   : CoreText, as on macOS
//   - Android: no-op for now (use bundled / system paths directly)
//
// Font::load() consults this layer automatically when given a name that isn't
// a usable file path, so end-users normally don't call these functions
// directly. They're public for font pickers / introspection.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace trussc {

// Resolve a font name to an absolute file path. Returns "" if unknown.
// `name` accepts whatever the platform's lookup API accepts — typically
// either a PostScript name ("HiraginoSans-W3") or a family / display name
// ("Hiragino Sans"). The PostScript form is the most portable.
TC_PLATFORMS("macos,windows,linux,ios") fs::path systemFontPath(const std::string& name);

// Enumerate the names of all fonts known to the OS. The exact form (PS name
// vs. family) is platform-specific; expect deduplicated family-style names
// in most cases. Returned vector may be empty if the platform backend is
// unavailable (Android, Web).
TC_PLATFORMS("macos,windows,linux,ios") std::vector<std::string> listSystemFonts();

namespace internal {

// The face a system font name resolves to: the file, and the index of the
// face inside it (its position in a font collection, .ttc / .otc; 0 for a
// single font).
struct SystemFontFace {
    fs::path path;
    int index = 0;
};

// Resolve a font name to a file and a face index, the same way the OS's
// lookup resolves it (systemFontPath() returns the path part). The path is
// empty when the name is unknown. The face index comes from the OS:
// fontconfig FC_INDEX on Linux, IDWriteFontFace::GetIndex() on Windows, and
// on macOS / iOS the face whose PostScript name matches the CoreText font's.
SystemFontFace systemFontFace(const std::string& name);

// Index of the face in font data (a single font or a collection) whose
// PostScript name (name ID 6) equals `postScriptName`, or -1 when no face
// has that name. Reads only within `size` bytes.
int findFaceByPostScriptName(const uint8_t* data, size_t size,
                             const std::string& postScriptName);

// The same for a font file: 0 for a single font (without reading its
// names), the matching face of a collection, or -1 when no face of the
// collection has that name or the file cannot be read. Seeks to the collection
// header, face directories and name tables; checks their ranges in 64 bits
// against the file size without reading the whole collection.
int findFaceInFileByPostScriptName(const fs::path& path, const std::string& postScriptName);

} // namespace internal

} // namespace trussc

namespace tc = trussc;
