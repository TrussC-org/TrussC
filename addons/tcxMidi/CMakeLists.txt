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
# Use extraction-time timestamps on CMake 3.24+, without requiring the newer
# DOWNLOAD_EXTRACT_TIMESTAMP argument on our supported CMake 3.20 minimum.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
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
#
# Pinned to the v5.4.3 release. Our WebMIDI fixes are upstreamed
# (celtera/libremidi PR #216) but not yet in a tagged release, so we keep the
# small export workaround below. (Pinning master directly is not viable yet -
# its emscripten backend currently fails to compile, unrelated to our PR.)
FetchContent_Declare(
    libremidi
    # A new source location forces fresh extraction on upgrade: R4 cannot undo R1-R3.
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/libremidi-v5.4.3-r4-src"
    URL      https://github.com/celtera/libremidi/archive/refs/tags/v5.4.3.tar.gz
    URL_HASH SHA256=a1c369c6aab01c80f1506345d1c099a167b330d1d59159aa7583f7b0b9f966b9
    PATCH_COMMAND ${CMAKE_COMMAND} -DLIBREMIDI_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_LIST_DIR}/cmake/PatchLibremidi.cmake
)
FetchContent_MakeAvailable(libremidi)
# Keep MSVC diagnostics quiet in fetched sources, including cmidi2.hpp when
# compiled by libremidi's own translation units. Do not pass /w to consumers.
if(MSVC)
    target_compile_options(libremidi PRIVATE /w)
endif()
# PATCH_COMMAND is skipped for source overrides and may be skipped in reused
# caches. Require upstream input files and the checked ALSA output patch.
execute_process(COMMAND ${CMAKE_COMMAND}
    -DLIBREMIDI_SOURCE_DIR=${libremidi_SOURCE_DIR} -DVERIFY_ONLY=ON
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/PatchLibremidi.cmake
    RESULT_VARIABLE _tcxMidi_patch_result
    ERROR_VARIABLE _tcxMidi_patch_error)
if(NOT _tcxMidi_patch_result EQUAL 0)
    message(FATAL_ERROR "[tcxMidi] ${_tcxMidi_patch_error}")
endif()

# v5.4.3's WebMIDI (Emscripten) backend calls Module._malloc / _free / HEAPU8 /
# UTF8ToString / stringToUTF8 / lengthBytesUTF8 from JS, but its own CMake only
# exports _free (forgetting _malloc) and no runtime methods - so receiving a MIDI
# message throws "Module._malloc is not a function". Re-declare a complete export
# list on the libremidi target (the last -sEXPORTED_FUNCTIONS wins). Fixed
# upstream in PR #216; remove this once we move to a release that includes it.
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
