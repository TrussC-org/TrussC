cmake_minimum_required(VERSION 3.20)
project(trusscli)

# CLI builds default to MinSizeRel (smallest binary); user app defaults stay in trussc_app.cmake.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

# Default path (for TrussC repository)
set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../core")

# Override with local config if exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.trussc")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.trussc" TRUSSC_DIR)
    string(STRIP "${TRUSSC_DIR}" TRUSSC_DIR)
endif()

include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

# Binary is trusscli (CLI-friendly name); the .app bundle advertises itself
# as "TrussC Project Generator" in Finder / Dock so the GUI mode still reads
# naturally. NAME is required because the folder is "tools" — without it
# trussc_app() would derive the binary name from the folder.
# trusscli is a CLI tool — always keep the console visible so stdout/stderr
# work properly in CMD, PowerShell, and terminal emulators.
add_compile_definitions(TRUSSC_SHOW_CONSOLE)

# Bake version info from the trusscli source repo (also the TrussC repo,
# since trusscli is part of the TrussC monorepo)
execute_process(
    COMMAND git describe --tags --always --dirty --match "v[0-9]*"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    OUTPUT_VARIABLE TRUSSCLI_VERSION_STR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT TRUSSCLI_VERSION_STR)
    set(TRUSSCLI_VERSION_STR "unknown")
endif()
# HEAD only: unrelated commits and working-tree edits do not change this id.
execute_process(
    COMMAND git rev-parse HEAD:tools/src HEAD:tools/CMakeLists.txt HEAD:tools/addons.make HEAD:tools/icon
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    RESULT_VARIABLE TRUSSCLI_SOURCE_ID_RESULT
    OUTPUT_VARIABLE TRUSSCLI_SOURCE_ID_STR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT TRUSSCLI_SOURCE_ID_RESULT STREQUAL "0" OR TRUSSCLI_VERSION_STR STREQUAL "unknown")
    set(TRUSSCLI_SOURCE_ID_STR "")
endif()
string(REPLACE "\n" "|" TRUSSCLI_SOURCE_ID_STR "${TRUSSCLI_SOURCE_ID_STR}")
string(TIMESTAMP TRUSSCLI_BUILD_DATE_STR "%Y-%m-%d")
trussc_app(
    NAME trusscli
    DISPLAY_NAME "TrussC Project Generator"
)

# These are only read in main.cpp. Directory-wide definitions also reach
# TrussC and addons, changing their compile commands on each commit/day.
set_source_files_properties(src/main.cpp PROPERTIES COMPILE_DEFINITIONS
    "TRUSSCLI_VERSION=\"${TRUSSCLI_VERSION_STR}\";TRUSSCLI_BUILD_DATE=\"${TRUSSCLI_BUILD_DATE_STR}\";TRUSSCLI_SOURCE_ID=\"${TRUSSCLI_SOURCE_ID_STR}\"")

# Allow callers to place build output outside the source tree.
foreach(prop RUNTIME_OUTPUT_DIRECTORY RUNTIME_OUTPUT_DIRECTORY_RELEASE
             RUNTIME_OUTPUT_DIRECTORY_DEBUG RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
             RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL)
    if(DEFINED CMAKE_${prop})
        set_target_properties(trusscli PROPERTIES ${prop} "${CMAKE_${prop}}")
    endif()
endforeach()
if(APPLE)
    target_link_options(trusscli PRIVATE "$<$<CONFIG:MinSizeRel>:-Wl,-dead_strip>")
    add_custom_command(TARGET trusscli POST_BUILD
        COMMAND "$<$<CONFIG:MinSizeRel>:${CMAKE_STRIP}>"
                "$<$<CONFIG:MinSizeRel>:-x>" "$<$<CONFIG:MinSizeRel>:$<TARGET_FILE:trusscli>>"
        COMMAND_EXPAND_LISTS VERBATIM)
elseif(MSVC)
    # MinSizeRel's standard /O1 /DNDEBUG flags omit /Zi and /DEBUG (and PDBs).
    target_link_options(trusscli PRIVATE "$<$<CONFIG:MinSizeRel>:/DEBUG:NONE>")
elseif(UNIX)
    target_link_options(trusscli PRIVATE "$<$<CONFIG:MinSizeRel>:-s>")
endif()

# v0.9.1 Home/Hub shell, alongside the existing GUI.
add_subdirectory(gui2)
