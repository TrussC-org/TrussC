cmake_minimum_required(VERSION 3.20)

# Default path (for TrussC repository)
set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")

# Override with local config if exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.trussc")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.trussc" TRUSSC_DIR)
    string(STRIP "${TRUSSC_DIR}" TRUSSC_DIR)
endif()

include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

trussc_app()
