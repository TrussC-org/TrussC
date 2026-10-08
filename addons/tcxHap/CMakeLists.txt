# =============================================================================
# tcxHap - TrussC HAP Video Codec Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxHap)

# Fetch dependencies to libs/ for sharing
include(FetchContent)

# -----------------------------------------------------------------------------
# Snappy (BSD 3-Clause)
# -----------------------------------------------------------------------------
FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.3.1
)
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(snappy)
if(EMSCRIPTEN)
    # snappy compiles itself with -Werror on Clang; its '#pragma clang loop'
    # vectorization hints cannot be honored under wasm, and the resulting
    # -Wpass-failed diagnostic would fail the build. Silence it on the
    # dependency target only.
    target_compile_options(snappy PRIVATE -Wno-pass-failed)
endif()

# Snappy adds -Werror for Clang builds. Xcode enables additional warnings
# (e.g. -Wshorten-64-to-32) that must not fail this third-party target.
# Keep failed-vectorization hints quiet on backends such as wasm/em++.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(snappy PRIVATE -Wno-pass-failed -Wno-error)
endif()

# -----------------------------------------------------------------------------
# HAP reference decoder (BSD 2-Clause)
# Note: Not a CMake project, so we fetch and compile source directly
# -----------------------------------------------------------------------------
FetchContent_Declare(
    hap_source
    GIT_REPOSITORY https://github.com/Vidvox/hap.git
    GIT_TAG d847f6bbd3be88575dd4ef33a877243780e3be76 # 2024-07-25
)
FetchContent_GetProperties(hap_source)
if(NOT hap_source_POPULATED)
    FetchContent_Populate(hap_source)
endif()

# HAP source files
set(HAP_SOURCES
    ${hap_source_SOURCE_DIR}/source/hap.c
)
set(HAP_HEADERS
    ${hap_source_SOURCE_DIR}/source/hap.h
)

# -----------------------------------------------------------------------------
# Addon source files
# -----------------------------------------------------------------------------
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

# Platform-specific sources
if(APPLE)
    file(GLOB PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*_mac.mm)
elseif(WIN32)
    file(GLOB PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*_win.cpp)
else()
    file(GLOB PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*_linux.cpp)
endif()

# -----------------------------------------------------------------------------
# Build addon library
# -----------------------------------------------------------------------------
add_library(${ADDON_NAME} STATIC
    ${ADDON_SOURCES}
    ${ADDON_HEADERS}
    ${PLATFORM_SOURCES}
    ${HAP_SOURCES}
    ${HAP_HEADERS}
)

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${hap_source_SOURCE_DIR}/source
)

target_link_libraries(${ADDON_NAME} PUBLIC snappy TrussC)

# Snappy requires C++ headers
target_include_directories(${ADDON_NAME} PRIVATE
    ${snappy_SOURCE_DIR}
    ${snappy_BINARY_DIR}
)

# -----------------------------------------------------------------------------
# Shader compilation with sokol-shdc (core/cmake/trussc_shaders.cmake)
# -----------------------------------------------------------------------------
trussc_compile_shaders(${ADDON_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")

# tcxHapPlayer.h includes "ycocg.glsl.h" from src/shaders
target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
