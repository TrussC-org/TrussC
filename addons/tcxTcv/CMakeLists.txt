# =============================================================================
# tcxTcv - TrussC Video Codec Addon
# =============================================================================
# Custom video codec optimized for UI recordings and motion graphics.
# Uses BC7 texture compression for efficient storage.

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxTcv)

# Fetch LZ4 via FetchContent
include(FetchContent)
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.10.0
)
FetchContent_MakeAvailable(lz4)

# Source files
file(GLOB ADDON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/impl/*.cpp
)
file(GLOB ADDON_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/impl/*.h
)

# Add LZ4 source files directly (header-only style usage)
set(LZ4_SOURCES
    ${lz4_SOURCE_DIR}/lib/lz4.c
)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS} ${LZ4_SOURCES})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${lz4_SOURCE_DIR}/lib
)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
