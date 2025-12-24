# =============================================================================
# trussc_app.cmake - TrussC application setup macro
# =============================================================================
#
# Usage:
#   set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
#   include(${TRUSSC_DIR}/cmake/trussc_app.cmake)
#   trussc_app()
#
# This is all you need to build a TrussC app.
# To use addons, list addon names in addons.make file.
#
# Options:
#   trussc_app(SOURCES file1.cpp file2.cpp)  # Explicit source files
#   trussc_app(NAME myapp)                    # Explicit project name
#
# Shader compilation:
#   If src/**/*.glsl files exist, they are automatically compiled
#   with sokol-shdc to generate cross-platform shader headers.
#
# =============================================================================

macro(trussc_app)
    # Set default build type to RelWithDebInfo
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()

    # Parse arguments
    set(_options "")
    set(_oneValueArgs NAME)
    set(_multiValueArgs SOURCES)
    cmake_parse_arguments(_TC_APP "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    # Project name (use folder name if not specified)
    if(_TC_APP_NAME)
        set(_TC_PROJECT_NAME ${_TC_APP_NAME})
    else()
        get_filename_component(_TC_PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    endif()

    # Set languages based on platform
    if(APPLE)
        project(${_TC_PROJECT_NAME} LANGUAGES C CXX OBJC OBJCXX)
    else()
        project(${_TC_PROJECT_NAME} LANGUAGES C CXX)
    endif()

    # C++20
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Add TrussC
    add_subdirectory(${TRUSSC_DIR} ${CMAKE_BINARY_DIR}/TrussC)

    # Source files (auto-collect from src/ recursively if not specified)
    if(_TC_APP_SOURCES)
        set(_TC_SOURCES ${_TC_APP_SOURCES})
    else()
        file(GLOB_RECURSE _TC_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
        )
    endif()

    # Preserve directory structure in Xcode / Visual Studio
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${_TC_SOURCES})

    # Create executable
    add_executable(${PROJECT_NAME} ${_TC_SOURCES})

    # Link TrussC
    target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

    # Apply addons from addons.make
    apply_addons(${PROJECT_NAME})

    # Compile shaders with sokol-shdc (if any .glsl files exist)
    file(GLOB_RECURSE _TC_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.glsl")
    if(_TC_SHADER_SOURCES)
        # Select sokol-shdc binary based on host platform
        # Download from official sokol-tools-bin repository
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
            if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
                set(_TC_SOKOL_SHDC_DIR "linux_arm64")
            else()
                set(_TC_SOKOL_SHDC_DIR "linux")
            endif()
        endif()
        # Windows uses .exe extension
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(_TC_SOKOL_SHDC_EXT ".exe")
        else()
            set(_TC_SOKOL_SHDC_EXT "")
        endif()
        set(_TC_SOKOL_SHDC_URL "${_TC_SOKOL_SHDC_BASE_URL}/${_TC_SOKOL_SHDC_DIR}/sokol-shdc${_TC_SOKOL_SHDC_EXT}")
        set(_TC_SOKOL_SHDC_NAME "sokol-shdc${_TC_SOKOL_SHDC_EXT}")

        set(_TC_SOKOL_SHDC "${TC_ROOT}/trussc/tools/sokol-shdc/${_TC_SOKOL_SHDC_NAME}")

        # Download sokol-shdc if not present
        if(NOT EXISTS "${_TC_SOKOL_SHDC}")
            message(STATUS "[${PROJECT_NAME}] Downloading sokol-shdc...")
            file(MAKE_DIRECTORY "${TC_ROOT}/trussc/tools/sokol-shdc")
            file(DOWNLOAD "${_TC_SOKOL_SHDC_URL}" "${_TC_SOKOL_SHDC}"
                SHOW_PROGRESS
                STATUS _TC_DOWNLOAD_STATUS)
            list(GET _TC_DOWNLOAD_STATUS 0 _TC_DOWNLOAD_ERROR)
            if(_TC_DOWNLOAD_ERROR)
                message(FATAL_ERROR "Failed to download sokol-shdc: ${_TC_DOWNLOAD_STATUS}")
            endif()
            # Make executable on Unix
            if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
                file(CHMOD "${_TC_SOKOL_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
            endif()
            message(STATUS "[${PROJECT_NAME}] sokol-shdc downloaded successfully")
        endif()

        # Output languages: Metal macOS, GLES3 for Web, WGSL for WebGPU
        set(_TC_SOKOL_SLANG "metal_macos:hlsl5:glsl300es:wgsl")

        set(_TC_SHADER_OUTPUTS "")
        foreach(_shader_src ${_TC_SHADER_SOURCES})
            set(_shader_out "${_shader_src}.h")
            list(APPEND _TC_SHADER_OUTPUTS ${_shader_out})

            get_filename_component(_shader_name ${_shader_src} NAME)
            add_custom_command(
                OUTPUT ${_shader_out}
                COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_out} -l ${_TC_SOKOL_SLANG} --ifdef
                DEPENDS ${_shader_src}
                COMMENT "Compiling shader: ${_shader_name}"
            )
        endforeach()

        add_custom_target(${PROJECT_NAME}_shaders DEPENDS ${_TC_SHADER_OUTPUTS})
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_shaders)
        message(STATUS "[${PROJECT_NAME}] Shader compilation enabled for ${_TC_SHADER_SOURCES}")
    endif()

    # Output settings
    if(EMSCRIPTEN)
        # Emscripten: HTML output
        set_target_properties(${PROJECT_NAME} PROPERTIES
            SUFFIX ".html"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        )
        # Custom shell HTML path
        set(_TC_SHELL_FILE "${TC_ROOT}/trussc/platform/web/shell.html")
        # WebGL2 link options
        target_link_options(${PROJECT_NAME} PRIVATE
            -sUSE_WEBGL2=1
            -sALLOW_MEMORY_GROWTH=1
            -sFULL_ES3=1
            --shell-file=${_TC_SHELL_FILE}
        )
        # Auto-preload bin/data folder if it exists
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/data")
            target_link_options(${PROJECT_NAME} PRIVATE
                --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/bin/data@/data
            )
            message(STATUS "[${PROJECT_NAME}] Preloading data folder for Emscripten")
        endif()
    elseif(APPLE)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            # Xcode: Generate scheme and set as default target
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        trussc_setup_icon(${PROJECT_NAME})
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        )
        # Visual Studio: Set as startup project
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
        # Windows: Setup icon
        trussc_setup_icon(${PROJECT_NAME})
    endif()

    message(STATUS "[${PROJECT_NAME}] TrussC app configured")
endmacro()
