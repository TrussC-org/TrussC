# =============================================================================
# use_addon.cmake - TrussC addon inclusion macros
# =============================================================================
#
# Usage 1: Use addons.make (recommended)
#   add_executable(myapp ...)
#   apply_addons(myapp)
#
#   List addon names one per line in the addons.make file:
#     tcxOsc
#     tcxBox2d
#
# Usage 2: Specify directly
#   add_executable(myapp ...)
#   use_addon(myapp tcxBox2d)
#
# This alone adds the tcxBox2d addon and links it to myapp.
#
# Addon structure:
#   1. If CMakeLists.txt exists -> use it (full control)
#   2. If CMakeLists.txt is absent -> auto-collect src/ and libs/
#
#   tcxSomeAddon/
#   ├── CMakeLists.txt  (optional - only when special handling is needed)
#   ├── src/            <- addon code (.h + .cpp)
#   └── libs/           <- external libraries (git submodules, etc.)
#       └── somelib/
#           ├── src/
#           └── include/
# =============================================================================

# Track which addons have already been added
set(_TC_LOADED_ADDONS "" CACHE INTERNAL "List of loaded TrussC addons")

# _tc_load_addon(addon_name)
# Create the addon's CMake target without linking it to anything. Used by
# both `use_addon` (which then links to the requested target) and the hot
# reload path (which links the addon's static lib directly to the guest).
macro(_tc_load_addon _ADDON_NAME)
    list(FIND _TC_LOADED_ADDONS ${_ADDON_NAME} _ADDON_INDEX)
    if(_ADDON_INDEX EQUAL -1)
        get_filename_component(_ADDON_PATH "${TRUSSC_DIR}/../addons/${_ADDON_NAME}" ABSOLUTE)

        if(NOT EXISTS "${_ADDON_PATH}")
            message(FATAL_ERROR "TrussC addon not found: ${_ADDON_NAME}\n  Looked in: ${_ADDON_PATH}")
        endif()

        if(EXISTS "${_ADDON_PATH}/CMakeLists.txt")
            add_subdirectory("${_ADDON_PATH}" "${CMAKE_BINARY_DIR}/addons/${_ADDON_NAME}")
        else()
            _tc_auto_addon(${_ADDON_NAME} "${_ADDON_PATH}")
        endif()

        list(APPEND _TC_LOADED_ADDONS ${_ADDON_NAME})
        set(_TC_LOADED_ADDONS "${_TC_LOADED_ADDONS}" CACHE INTERNAL "List of loaded TrussC addons")
    endif()
endmacro()

# use_addon(target addon_name [addon_name2 ...])
# Add a TrussC addon to a target and link it
macro(use_addon TARGET_NAME)
    foreach(_ADDON_NAME ${ARGN})
        _tc_load_addon(${_ADDON_NAME})
        target_link_libraries(${TARGET_NAME} PRIVATE ${_ADDON_NAME})
        # Treat the addon's headers as system includes in consumers, so the app's
        # opt-in -Wall/-Wextra (`trusscli build --warnings`) flags only the user's
        # own code — not addon headers (incl. full-control addons that declare
        # their own non-SYSTEM includes, e.g. tcxImGui). SYSTEM target property
        # is CMake 3.25+; older versions silently fall back to the per-include
        # SYSTEM in _tc_auto_addon (covers header-only/auto addons).
        if(NOT CMAKE_VERSION VERSION_LESS 3.25)
            set_target_properties(${_ADDON_NAME} PROPERTIES SYSTEM ON)
        endif()
    endforeach()
endmacro()

# =============================================================================
# Internal macro: add an addon via auto-collection
# =============================================================================
macro(_tc_auto_addon ADDON_NAME ADDON_PATH)
    # Collect source files
    set(_ADDON_SOURCES "")

    # Collect sources from src/
    if(EXISTS "${ADDON_PATH}/src")
        file(GLOB_RECURSE _SRC_FILES
            "${ADDON_PATH}/src/*.cpp"
            "${ADDON_PATH}/src/*.c"
            "${ADDON_PATH}/src/*.mm"
            "${ADDON_PATH}/src/*.m"
        )
        list(APPEND _ADDON_SOURCES ${_SRC_FILES})
    endif()

    # Collect sources from libs/
    if(EXISTS "${ADDON_PATH}/libs")
        file(GLOB _LIB_DIRS LIST_DIRECTORIES true "${ADDON_PATH}/libs/*")
        foreach(_LIB_DIR ${_LIB_DIRS})
            if(IS_DIRECTORY ${_LIB_DIR})
                if(EXISTS "${_LIB_DIR}/src")
                    file(GLOB_RECURSE _LIB_SRC_FILES
                        "${_LIB_DIR}/src/*.cpp"
                        "${_LIB_DIR}/src/*.c"
                        "${_LIB_DIR}/src/*.mm"
                        "${_LIB_DIR}/src/*.m"
                    )
                    list(APPEND _ADDON_SOURCES ${_LIB_SRC_FILES})
                endif()
            endif()
        endforeach()
    endif()

    # If there are no sources, create it as a header-only library
    if(_ADDON_SOURCES)
        add_library(${ADDON_NAME} STATIC ${_ADDON_SOURCES})
    else()
        add_library(${ADDON_NAME} INTERFACE)
    endif()

    # Set up include paths
    set(_ADDON_INCLUDE_DIRS "")

    # include/ folder
    if(EXISTS "${ADDON_PATH}/include")
        list(APPEND _ADDON_INCLUDE_DIRS "${ADDON_PATH}/include")
    endif()

    # src/ folder (when it contains headers)
    if(EXISTS "${ADDON_PATH}/src")
        list(APPEND _ADDON_INCLUDE_DIRS "${ADDON_PATH}/src")
    endif()

    # libs/*/include folders
    if(EXISTS "${ADDON_PATH}/libs")
        file(GLOB _LIB_DIRS LIST_DIRECTORIES true "${ADDON_PATH}/libs/*")
        foreach(_LIB_DIR ${_LIB_DIRS})
            if(IS_DIRECTORY ${_LIB_DIR})
                if(EXISTS "${_LIB_DIR}/include")
                    list(APPEND _ADDON_INCLUDE_DIRS "${_LIB_DIR}/include")
                endif()
                if(EXISTS "${_LIB_DIR}/src")
                    list(APPEND _ADDON_INCLUDE_DIRS "${_LIB_DIR}/src")
                endif()
            endif()
        endforeach()
    endif()

    # Apply include paths.
    # SYSTEM: addon headers are library code the app author didn't write, so
    # keep them out of the app's -Wall/-Wextra (the --warnings opt-in) — only the
    # user's own sources get flagged, same as core TrussC/sokol/stb headers.
    if(_ADDON_SOURCES)
        target_include_directories(${ADDON_NAME} SYSTEM PUBLIC ${_ADDON_INCLUDE_DIRS})
        target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
    else()
        target_include_directories(${ADDON_NAME} SYSTEM INTERFACE ${_ADDON_INCLUDE_DIRS})
    endif()

    message(STATUS "[${ADDON_NAME}] Auto-configured addon")
endmacro()

# =============================================================================
# apply_addons - automatically apply addons from addons.make
# =============================================================================
# Read the addons.make file and apply every addon listed in it.
# Comments (lines starting with #) and blank lines are ignored.
macro(apply_addons TARGET_NAME)
    set(_ADDONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/addons.make")
    if(EXISTS "${_ADDONS_FILE}")
        file(STRINGS "${_ADDONS_FILE}" _ADDON_LINES)
        foreach(_LINE ${_ADDON_LINES})
            # Trim whitespace
            string(STRIP "${_LINE}" _LINE)
            # Skip blank lines and comment lines
            if(_LINE AND NOT _LINE MATCHES "^#")
                use_addon(${TARGET_NAME} ${_LINE})
            endif()
        endforeach()
    endif()
endmacro()

# =============================================================================
# tc_addon_bundle_file - bundle the runtime files an addon needs into the app
# =============================================================================
# When called from an addon's CMakeLists.txt, this bundles the specified files
# into the final app output. They are copied to the appropriate location per
# platform:
#   - macOS : App.app/Contents/<MACOS_DEST>  (default: Resources)
#   - Windows / Linux : the same directory as the executable (to place DLL/so
#     next to it)
#
# The actual copy is done by trussc_app() (the same scope as the app target),
# so here we only register them as a GLOBAL property to avoid cross-directory
# target modifications.
#
# Usage (inside an addon's CMakeLists.txt):
#   tc_addon_bundle_file(${CMAKE_CURRENT_BINARY_DIR}/default.metallib MACOS_DEST Resources)
#   tc_addon_bundle_file(${CMAKE_CURRENT_SOURCE_DIR}/libs/sensor/sensor.dll)
function(tc_addon_bundle_file _FILE)
    cmake_parse_arguments(_TCB "" "MACOS_DEST" "" ${ARGN})
    if(NOT _TCB_MACOS_DEST)
        set(_TCB_MACOS_DEST "Resources")
    endif()
    get_filename_component(_TCB_ABS "${_FILE}" ABSOLUTE)
    # Accumulate in the form "<macos-dest>|<absolute-path>" (paths are assumed not to contain |)
    set_property(GLOBAL APPEND PROPERTY TC_ADDON_BUNDLE_FILES "${_TCB_MACOS_DEST}|${_TCB_ABS}")
endfunction()
