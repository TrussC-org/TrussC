cmake_minimum_required(VERSION 3.20)
project(trusscli)

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
trussc_app(
    NAME trusscli
    DISPLAY_NAME "TrussC Project Generator"
)
