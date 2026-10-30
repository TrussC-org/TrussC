# =============================================================================
# tcxObj - TrussC OBJ file import/export addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxObj)

file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)

# Local OBJ/MTL files are bounded by addressable storage, not a 256 MiB policy.
# Keep upstream's reader and its vector/stream representation checks intact.
target_compile_definitions(${ADDON_NAME} PRIVATE
    "TINYOBJLOADER_STREAM_READER_MAX_BYTES=(size_t(-1))"
)

# STL allocation failures must reach the loader's recovery path on Web too.
if(EMSCRIPTEN)
    target_compile_options(${ADDON_NAME} PRIVATE -fexceptions)
    target_link_options(${ADDON_NAME} INTERFACE -fexceptions -sABORTING_MALLOC=0)
endif()
