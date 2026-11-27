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

# trusscli resolves versions before configuring. This map is also usable by
# manual CMake callers: -DTRUSSC_ADDON_PATHS='{"tcxFoo":"/path/to/tcxFoo"}'.
function(_tc_addon_path ADDON_NAME OUTPUT)
    if(TRUSSC_ADDON_PATHS)
        string(JSON _TC_PATH ERROR_VARIABLE _TC_PATH_ERROR GET "${TRUSSC_ADDON_PATHS}" "${ADDON_NAME}")
        if(NOT _TC_PATH_ERROR)
            set(${OUTPUT} "${_TC_PATH}" PARENT_SCOPE)
            return()
        endif()
    endif()
    get_filename_component(_TC_PATH "${TRUSSC_DIR}/../addons/${ADDON_NAME}" ABSOLUTE)
    set(${OUTPUT} "${_TC_PATH}" PARENT_SCOPE)
endfunction()

# Share the normalized selection with normal and hot-reload app builds.
function(_tc_project_addons OUTPUT)
    set(_TC_NAMES "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/addons.make")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/addons.make" _TC_LINES)
        foreach(_TC_LINE IN LISTS _TC_LINES)
            string(STRIP "${_TC_LINE}" _TC_LINE)
            if(_TC_LINE AND NOT _TC_LINE MATCHES "^#")
                string(REGEX REPLACE "@.*$" "" _TC_NAME "${_TC_LINE}")
                string(REGEX REPLACE "^.*/" "" _TC_NAME "${_TC_NAME}")
                if(NOT TRUSSC_ADDON_PATHS AND NOT _TC_LINE STREQUAL _TC_NAME)
                    message(FATAL_ERROR "Addon versions require resolution; run trusscli update, then trusscli build")
                endif()
                list(APPEND _TC_NAMES "${_TC_NAME}")
            endif()
        endforeach()
    endif()
    if(TRUSSC_ADDON_PATHS)
        string(JSON _TC_COUNT LENGTH "${TRUSSC_ADDON_PATHS}")
        if(_TC_COUNT GREATER 0)
            math(EXPR _TC_LAST "${_TC_COUNT} - 1")
            foreach(_TC_INDEX RANGE ${_TC_LAST})
                string(JSON _TC_NAME MEMBER "${TRUSSC_ADDON_PATHS}" ${_TC_INDEX})
                list(APPEND _TC_NAMES "${_TC_NAME}")
            endforeach()
        endif()
    endif()
    if(TRUSSC_ADDON_ORDER)
        # The resolver supplies parent-before-dependency order for static archives.
        set(_TC_NAMES ${TRUSSC_ADDON_ORDER} ${_TC_NAMES})
    endif()
    list(REMOVE_DUPLICATES _TC_NAMES)
    set(${OUTPUT} "${_TC_NAMES}" PARENT_SCOPE)
endfunction()

# _tc_load_addon(addon_name)
# Create the addon's CMake target without linking it to anything. Used by
# both `use_addon` (which then links to the requested target) and the hot
# reload path (which links the addon's static lib directly to the guest).
macro(_tc_load_addon _ADDON_NAME)
    # Reserve the selection before add_subdirectory: a transitive load can
    # happen before the first addon has created its target or entered the list.
    if("${_ADDON_NAME}" STREQUAL "tcxBox2d" OR "${_ADDON_NAME}" STREQUAL "tcxBox2d3")
        get_property(_TC_BOX2D_SELECTED GLOBAL PROPERTY _TC_BOX2D_SELECTED)
        if(_TC_BOX2D_SELECTED AND NOT _TC_BOX2D_SELECTED STREQUAL "${_ADDON_NAME}")
            message(FATAL_ERROR "tcxBox2d and tcxBox2d3 cannot be used together; choose one in addons.make.")
        endif()
        set_property(GLOBAL PROPERTY _TC_BOX2D_SELECTED "${_ADDON_NAME}")
    endif()
    list(FIND _TC_LOADED_ADDONS ${_ADDON_NAME} _ADDON_INDEX)
    if(_ADDON_INDEX EQUAL -1)
        _tc_addon_path("${_ADDON_NAME}" _ADDON_PATH)

        if(NOT EXISTS "${_ADDON_PATH}")
            message(FATAL_ERROR "TrussC addon not found: ${_ADDON_NAME}\n  Looked in: ${_ADDON_PATH}")
        endif()

        if(EXISTS "${_ADDON_PATH}/CMakeLists.txt")
            add_subdirectory("${_ADDON_PATH}" "${CMAKE_BINARY_DIR}/addons/${_ADDON_NAME}")
        else()
            _tc_auto_addon(${_ADDON_NAME} "${_ADDON_PATH}")
        endif()

        # Keep TrussC.h's /subsystem:windows pragma out of addon library objects
        # (see the guard in TrussC.h): a single such .drectve in a linked static
        # lib overrides the command-line /SUBSYSTEM and flips a console tool to
        # the GUI subsystem. This is the one chokepoint every addon passes through
        # regardless of how its target was created (own CMakeLists or auto). Skip
        # header-only INTERFACE addons — they compile no TUs of their own.
        if(WIN32 AND TARGET ${_ADDON_NAME})
            get_target_property(_TC_ADDON_TYPE ${_ADDON_NAME} TYPE)
            if(NOT _TC_ADDON_TYPE STREQUAL "INTERFACE_LIBRARY")
                target_compile_definitions(${_ADDON_NAME} PRIVATE TRUSSC_LIBRARY_TU)
            endif()
        endif()

        # Windows + Ninja: recompile the addon's objects in the same build as
        # an edited shader (see trussc_shader_object_depends). Done here so an
        # addon's CMakeLists.txt only links its <name>_shaders target.
        if(TARGET ${_ADDON_NAME} AND TARGET ${_ADDON_NAME}_shaders)
            trussc_shader_object_depends(${_ADDON_NAME} ${_ADDON_NAME}_shaders)
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

    # CONFIGURE_DEPENDS: an existing build tree re-globs on each build, so a
    # source added to the addon (e.g. by a git pull) is picked up without a
    # manual reconfigure (#375).
    # Collect sources from src/
    if(EXISTS "${ADDON_PATH}/src")
        file(GLOB_RECURSE _SRC_FILES CONFIGURE_DEPENDS
            "${ADDON_PATH}/src/*.cpp"
            "${ADDON_PATH}/src/*.c"
            "${ADDON_PATH}/src/*.mm"
            "${ADDON_PATH}/src/*.m"
        )
        list(APPEND _ADDON_SOURCES ${_SRC_FILES})
    endif()

    # Collect sources from libs/
    if(EXISTS "${ADDON_PATH}/libs")
        file(GLOB _LIB_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${ADDON_PATH}/libs/*")
        foreach(_LIB_DIR ${_LIB_DIRS})
            if(IS_DIRECTORY ${_LIB_DIR})
                if(EXISTS "${_LIB_DIR}/src")
                    file(GLOB_RECURSE _LIB_SRC_FILES CONFIGURE_DEPENDS
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
        file(GLOB _LIB_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${ADDON_PATH}/libs/*")
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
    _tc_project_addons(_TC_PROJECT_ADDONS)
    foreach(_TC_PROJECT_ADDON IN LISTS _TC_PROJECT_ADDONS)
        use_addon(${TARGET_NAME} ${_TC_PROJECT_ADDON})
    endforeach()
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
