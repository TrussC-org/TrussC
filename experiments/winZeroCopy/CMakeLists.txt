cmake_minimum_required(VERSION 3.20)
project(winZeroCopy)

# Configure directly; the harness needs no generated project metadata.
if(NOT DEFINED TRUSSC_DIR)
    set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../core")
endif()
if(NOT EXISTS "${TRUSSC_DIR}/cmake/trussc_app.cmake")
    message(FATAL_ERROR "Configure with -DTRUSSC_DIR=<TrussC-root>/core")
endif()
include("${TRUSSC_DIR}/cmake/trussc_app.cmake")
trussc_app()
target_compile_definitions(winZeroCopy PRIVATE TRUSSC_SHOW_CONSOLE)
if(WIN32)
    target_link_libraries(winZeroCopy PRIVATE psapi)
endif()

execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE WINEXP_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(WINEXP_SHA)
    target_compile_definitions(winZeroCopy PRIVATE TRUSSC_EXP_BUILD_SHA="${WINEXP_SHA}")
endif()
