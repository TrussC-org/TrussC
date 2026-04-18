# =============================================================================
# tcxGltf - TrussC glTF 2.0 Loader Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxGltf)

# Fetch cgltf (single-header C glTF parser)
include(FetchContent)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
)
FetchContent_MakeAvailable(cgltf)

# Source files
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# cgltf is header-only; just add include path
target_include_directories(${ADDON_NAME} PRIVATE
    ${cgltf_SOURCE_DIR}
)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
