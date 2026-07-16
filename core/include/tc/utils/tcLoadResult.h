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
//     if (!snd.load("beep.wav")) {
//         logError() << snd.load("beep.wav").message;   // (illustrative)
//     }
//
// Error taxonomy is deliberately coarse for now (v0.7): the enum can gain
// values and messages can get richer without breaking anything. What CAN'T
// change compatibly is the return type — which is why the shape lands in
// the v0.7 breaking window.
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
        case LoadError::Unknown:           return "Unknown";
    }
    return "Unknown";
}

} // namespace trussc
