#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// LoadResult — unified result type for resource loading (Image, Pixels,
// SoundBuffer, SoundPlayer, VideoPlayer, Font, ...).
//
// Replaces the old `bool` returns. `explicit operator bool()` keeps the
// common pattern working unchanged:
//
//     if (img.load("photo.png")) { ... }        // still fine
//
// File loaders resolve relative paths via getDataPath() and share one
// missing-file report. Font first tries a data file, then a system font name.
// Existing getPath() accessors are empty after failure or a memory load.
// The taxonomy can grow without changing the result shape.
// =============================================================================

#include <string>

namespace trussc {

// What went wrong during a load. None == success.
enum class LoadError {
    None = 0,           // success
    FileNotFound,       // the (resolved) path does not exist / could not be opened
    UnsupportedFormat,  // extension / container not supported on this platform
    DecodeFailed,       // the decoder rejected the data (corrupt / truncated / wrong codec)
    Unknown,            // anything the loader could not classify
    TooLarge,           // exceeds a resource size or admission limit
    Cancelled,          // loading was cancelled
};

// Result of a load operation. Truthy on success.
struct LoadResult {
    LoadError error = LoadError::None;
    std::string message;    // human-readable detail; may be empty

    bool ok() const { return error == LoadError::None; }
    explicit operator bool() const { return ok(); }

    static LoadResult success() { return {}; }
    static LoadResult fail(LoadError e, std::string msg = {}) {
        return LoadResult{e, std::move(msg)};
    }
};

// Short label for a LoadError value ("FileNotFound", ...). For log messages.
inline const char* loadErrorName(LoadError e) {
    switch (e) {
        case LoadError::None:              return "None";
        case LoadError::FileNotFound:      return "FileNotFound";
        case LoadError::UnsupportedFormat: return "UnsupportedFormat";
        case LoadError::DecodeFailed:      return "DecodeFailed";
        case LoadError::TooLarge:          return "TooLarge";
        case LoadError::Cancelled:         return "Cancelled";
        case LoadError::Unknown:           return "Unknown";
    }
    return "Unknown";
}

} // namespace trussc
