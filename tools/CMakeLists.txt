cmake_minimum_required(VERSION 3.20)
project(projectGenerator)

# Default path (for TrussC repository)
set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../core")

# Override with local config if exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.trussc")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.trussc" TRUSSC_DIR)
    string(STRIP "${TRUSSC_DIR}" TRUSSC_DIR)
endif()

include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

# NAME is required because the tool lives in tools/ — without it, trussc_app()
# would derive the binary name from the folder ("tools") instead.
trussc_app(NAME projectGenerator)
