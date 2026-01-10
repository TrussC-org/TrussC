# =============================================================================
# tcxLut - TrussC 3D LUT Color Grading Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxLut)

# Header-only addon
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} INTERFACE)

target_include_directories(${ADDON_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${ADDON_NAME} INTERFACE TrussC)
