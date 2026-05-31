# =============================================================================
# tcxMidi - TrussC MIDI Addon
# =============================================================================
# Header-only TrussC wrapper around libremidi (fetched via FetchContent).
# Backends: CoreMIDI (macOS/iOS), WinMM (Windows), ALSA (Linux),
#           WebMIDI (Emscripten), AMidi (Android - needs API level 31+).
# =============================================================================

cmake_minimum_required(VERSION 3.20)

set(ADDON_NAME tcxMidi)
message(STATUS "[tcxMidi] Loading addon...")

# -----------------------------------------------------------------------------
# Fetch libremidi
# -----------------------------------------------------------------------------
include(FetchContent)

# Keep the dependency footprint small and predictable: use std types instead of
# Boost, build only the core platform backends, skip examples/tests. JACK,
# PipeWire and the network backend are optional system deps - re-enable them in
# your project if you need them.
set(LIBREMIDI_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_BOOST     ON  CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_JACK      ON  CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_PIPEWIRE  ON  CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_NETWORK   ON  CACHE BOOL "" FORCE)
set(LIBREMIDI_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_TESTS        OFF CACHE BOOL "" FORCE)

# URL (release tarball) instead of GIT_REPOSITORY: works even without git
# installed, and the archive caches/extracts faster than a clone. URL_HASH
# guards against a corrupted or tampered download.
FetchContent_Declare(
    libremidi
    URL      https://github.com/celtera/libremidi/archive/refs/tags/v5.4.3.tar.gz
    URL_HASH SHA256=a1c369c6aab01c80f1506345d1c099a167b330d1d59159aa7583f7b0b9f966b9
)
FetchContent_MakeAvailable(libremidi)

# libremidi's WebMIDI (Emscripten) backend calls Module._malloc / _free / HEAPU8
# / UTF8ToString / stringToUTF8 / lengthBytesUTF8 from JS, but its own CMake only
# exports _free (forgetting _malloc) and no runtime methods - so receiving a MIDI
# message throws "Module._malloc is not a function". Re-declare a complete export
# list on the libremidi target (the last -sEXPORTED_FUNCTIONS wins) so the web
# build actually works at runtime.
if(EMSCRIPTEN)
    target_link_options(libremidi PUBLIC
        "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_libremidi_devices_poll,_libremidi_devices_input"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8"
    )
endif()

# -----------------------------------------------------------------------------
# tcxMidi target (header-only wrapper)
# -----------------------------------------------------------------------------
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} INTERFACE)

target_sources(${ADDON_NAME} INTERFACE ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${ADDON_NAME} INTERFACE TrussC libremidi::libremidi)

message(STATUS "[tcxMidi] Addon loaded")
