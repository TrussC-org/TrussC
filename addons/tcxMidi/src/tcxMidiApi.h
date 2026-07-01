#pragma once

// =============================================================================
// tcxMidiApi.h - pick the platform's native libremidi backend
// =============================================================================
// libremidi's default (no-API) observer/midi_in/midi_out picks the FIRST
// backend in its compiled list. On some platforms (notably Android) that first
// entry is the "Computer keyboard" backend, so device enumeration silently
// returns nothing. We therefore pin the platform's real MIDI API explicitly.
//
// libremidi::midi1::default_api() returns the right API for macOS / Windows /
// Linux / Android, but on Emscripten its internal check uses the wrong macro
// (`__emscripten__` instead of `__EMSCRIPTEN__`) and falls through to DUMMY, so
// we special-case the web here.
// =============================================================================

#include <libremidi/libremidi.hpp>

namespace tcx::midi {

using namespace tc;

inline libremidi::API platformMidiApi() {
#if defined(__EMSCRIPTEN__)
    return libremidi::API::WEBMIDI;
#else
    return libremidi::midi1::default_api();
#endif
}

}  // namespace tcx::midi
