#pragma once

// =============================================================================
// tcVersion.h - TrussC version reporting
// =============================================================================
//
// The version's single source of truth is the git tag: the build bakes
// `git describe` output into TC_VERSION_STRING (see core/CMakeLists.txt).
// Source archives without .git get it from .git_archival.txt (export-subst).
// No version number lives in source code.

#ifndef TC_VERSION_STRING
#define TC_VERSION_STRING "unknown"   // direct include without the CMake build
#endif

namespace trussc {

// TrussC version as reported by `git describe`, e.g. "v0.6.2" on a tagged
// release or "v0.6.2-14-gabc123" for a build 14 commits past it.
inline const char* getVersion() { return TC_VERSION_STRING; }

} // namespace trussc
