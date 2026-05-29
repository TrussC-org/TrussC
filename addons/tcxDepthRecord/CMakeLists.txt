# =============================================================================
# tcxDepthRecord - record & replay depth recordings (.tcdc)
# =============================================================================
#
# Header-only INTERFACE addon. Compression uses core's tc::compress (LZ4), so it
# links no third-party libs. Depends on tcxDepthCamera (the canonical DepthFrame
# + DepthCamera base it records/replays).
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxDepthRecord)

add_library(${ADDON_NAME} INTERFACE)

target_include_directories(${ADDON_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # tcxDepthCamera is a header-only interface addon next to us.
    ${CMAKE_CURRENT_SOURCE_DIR}/../tcxDepthCamera/src
)

target_link_libraries(${ADDON_NAME} INTERFACE TrussC)
