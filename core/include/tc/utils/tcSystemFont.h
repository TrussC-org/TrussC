#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// System font lookup — resolve a font name (PostScript name or family/display
// name, depending on platform) to a file path that can be passed to Font::load.
// Also enumerate the installed fonts the OS knows about.
//
// Backends:
//   - macOS : CoreText (CTFontDescriptor)
//   - Linux : fontconfig (planned; current impl is a stub)
//   - Win   : DirectWrite (planned; current impl is a stub)
//   - Web   : no-op (return empty; Web font loading is URL-based)
//   - iOS / Android: no-op for now (use bundled / system paths directly)
//
// Font::load() consults this layer automatically when given a name that isn't
// a usable file path, so end-users normally don't call these functions
// directly. They're public for font pickers / introspection.
// =============================================================================

#include <string>
#include <vector>

namespace trussc {

// Resolve a font name to an absolute file path. Returns "" if unknown.
// `name` accepts whatever the platform's lookup API accepts — typically
// either a PostScript name ("HiraginoSans-W3") or a family / display name
// ("Hiragino Sans"). The PostScript form is the most portable.
TC_PLATFORMS("macos,windows,linux,ios") std::string systemFontPath(const std::string& name);

// Enumerate the names of all fonts known to the OS. The exact form (PS name
// vs. family) is platform-specific; expect deduplicated family-style names
// in most cases. Returned vector may be empty if the platform backend is
// unavailable or has no implementation yet (Linux / Windows stubs).
TC_PLATFORMS("macos,windows,linux,ios") std::vector<std::string> listSystemFonts();

} // namespace trussc

namespace tc = trussc;
