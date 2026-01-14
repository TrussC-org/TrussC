# =============================================================================
# tcxTcv - TrussC Video Codec Addon
# =============================================================================
# Custom video codec optimized for UI recordings and motion graphics.
# Uses BC7 texture compression for efficient storage.

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxTcv)

# Source files
file(GLOB ADDON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/impl/*.cpp
)
file(GLOB ADDON_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/impl/*.h
)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
