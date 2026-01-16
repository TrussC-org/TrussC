# =============================================================================
# tcxHap - TrussC HAP Video Codec Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxHap)

# Fetch dependencies to libs/ for sharing
include(FetchContent)

# Allow older CMake versions in dependencies (Snappy uses 2.4)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# -----------------------------------------------------------------------------
# Snappy (BSD 3-Clause)
# -----------------------------------------------------------------------------
FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.2.1
)
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(snappy)

# -----------------------------------------------------------------------------
# HAP reference decoder (BSD 2-Clause)
# Note: Not a CMake project, so we fetch and compile source directly
# -----------------------------------------------------------------------------
FetchContent_Declare(
    hap_source
    GIT_REPOSITORY https://github.com/Vidvox/hap.git
    GIT_TAG master
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
# Shader compilation with sokol-shdc
# -----------------------------------------------------------------------------
file(GLOB_RECURSE ADDON_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.glsl")

if(ADDON_SHADER_SOURCES)
    # Select sokol-shdc binary based on host platform
    set(_TC_SOKOL_SHDC_BASE_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(_TC_SOKOL_SHDC_DIR "osx_arm64")
        else()
            set(_TC_SOKOL_SHDC_DIR "osx")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_TC_SOKOL_SHDC_DIR "win32")
    else()
        set(_TC_SOKOL_SHDC_DIR "linux")
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_TC_SOKOL_SHDC_EXT ".exe")
    else()
        set(_TC_SOKOL_SHDC_EXT "")
    endif()
    set(_TC_SOKOL_SHDC_URL "${_TC_SOKOL_SHDC_BASE_URL}/${_TC_SOKOL_SHDC_DIR}/sokol-shdc${_TC_SOKOL_SHDC_EXT}")
    set(_TC_SOKOL_SHDC_NAME "sokol-shdc${_TC_SOKOL_SHDC_EXT}")

    # Use TrussC's tools directory for sokol-shdc
    get_target_property(_TC_ROOT TrussC INTERFACE_INCLUDE_DIRECTORIES)
    list(GET _TC_ROOT 0 _TC_ROOT_DIR)
    get_filename_component(_TC_ROOT_DIR "${_TC_ROOT_DIR}" DIRECTORY)
    set(_TC_SOKOL_SHDC "${_TC_ROOT_DIR}/tools/sokol-shdc/${_TC_SOKOL_SHDC_NAME}")

    # Download sokol-shdc if not present
    if(NOT EXISTS "${_TC_SOKOL_SHDC}")
        message(STATUS "[${ADDON_NAME}] Downloading sokol-shdc...")
        file(MAKE_DIRECTORY "${_TC_ROOT_DIR}/tools/sokol-shdc")
        file(DOWNLOAD "${_TC_SOKOL_SHDC_URL}" "${_TC_SOKOL_SHDC}"
            SHOW_PROGRESS
            STATUS _TC_DOWNLOAD_STATUS)
        list(GET _TC_DOWNLOAD_STATUS 0 _TC_DOWNLOAD_ERROR)
        if(_TC_DOWNLOAD_ERROR)
            message(FATAL_ERROR "Failed to download sokol-shdc: ${_TC_DOWNLOAD_STATUS}")
        endif()
        if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            file(CHMOD "${_TC_SOKOL_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        endif()
        message(STATUS "[${ADDON_NAME}] sokol-shdc downloaded successfully")
    endif()

    # Output languages
    set(_TC_SOKOL_SLANG "metal_macos:hlsl5:glsl300es:wgsl")

    # Compile each shader
    foreach(_SHADER_SRC ${ADDON_SHADER_SOURCES})
        get_filename_component(_SHADER_NAME ${_SHADER_SRC} NAME)
        set(_SHADER_OUTPUT "${_SHADER_SRC}.h")

        add_custom_command(
            OUTPUT "${_SHADER_OUTPUT}"
            COMMAND "${_TC_SOKOL_SHDC}"
                --input "${_SHADER_SRC}"
                --output "${_SHADER_OUTPUT}"
                --slang ${_TC_SOKOL_SLANG}
                --format sokol
            DEPENDS "${_SHADER_SRC}"
            COMMENT "[${ADDON_NAME}] Compiling shader: ${_SHADER_NAME}"
        )
        list(APPEND ADDON_SHADER_OUTPUTS "${_SHADER_OUTPUT}")
    endforeach()

    # Add custom target for shaders
    add_custom_target(${ADDON_NAME}_shaders DEPENDS ${ADDON_SHADER_OUTPUTS})
    add_dependencies(${ADDON_NAME} ${ADDON_NAME}_shaders)

    # Include shader output directory
    target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
endif()
