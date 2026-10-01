# =============================================================================
# trussc_shaders.cmake - shader compilation with sokol-shdc
# =============================================================================
#
# Usage:
#   trussc_compile_shaders(<target> <source dir> [OUTPUT_DIR <dir>])
#
# Compiles every *.glsl under <source dir> (recursively) with sokol-shdc into
# <name>.glsl.h, next to the source, or in OUTPUT_DIR when given. A custom
# target <target>_shaders builds the headers, and <target> depends on it.
# On an INTERFACE library (header-only addon) the dependency is followed by
# every target that links it. Does nothing when <source dir> has no *.glsl.
#
# Used by core (built-in shaders), trussc_app() (src/ of the app) and addons
# (e.g. tcxHap, tcxLut).
# =============================================================================

include_guard(GLOBAL)

function(trussc_compile_shaders _TC_TARGET _TC_SOURCE_DIR)
    cmake_parse_arguments(_TC_SHADERS "" "OUTPUT_DIR" "" ${ARGN})

    # CONFIGURE_DEPENDS: an existing build tree re-globs on each build, so a
    # newly added shader gets its sokol-shdc command (and its header) without
    # a manual reconfigure (#366).
    file(GLOB_RECURSE _TC_SHADER_SOURCES CONFIGURE_DEPENDS "${_TC_SOURCE_DIR}/*.glsl")
    if(NOT _TC_SHADER_SOURCES)
        return()
    endif()

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

    # One shared copy in core/tools/sokol-shdc (this file is in core/cmake)
    get_filename_component(_TC_SOKOL_SHDC_TOOLS_DIR
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/sokol-shdc" ABSOLUTE)
    set(_TC_SOKOL_SHDC "${_TC_SOKOL_SHDC_TOOLS_DIR}/sokol-shdc${_TC_SOKOL_SHDC_EXT}")

    # Download sokol-shdc if not present.
    # raw.githubusercontent.com rate-limits anonymous requests (HTTP 429),
    # especially from shared CI runner IPs, so retry with a pause before
    # giving up. A failed file(DOWNLOAD) leaves a 0-byte file behind, which
    # would satisfy the EXISTS guard on the next configure — remove it.
    if(NOT EXISTS "${_TC_SOKOL_SHDC}")
        message(STATUS "[${_TC_TARGET}] Downloading sokol-shdc...")
        file(MAKE_DIRECTORY "${_TC_SOKOL_SHDC_TOOLS_DIR}")
        foreach(_TC_ATTEMPT RANGE 1 3)
            file(DOWNLOAD "${_TC_SOKOL_SHDC_URL}" "${_TC_SOKOL_SHDC}"
                SHOW_PROGRESS
                STATUS _TC_DOWNLOAD_STATUS)
            list(GET _TC_DOWNLOAD_STATUS 0 _TC_DOWNLOAD_ERROR)
            if(NOT _TC_DOWNLOAD_ERROR)
                break()
            endif()
            file(REMOVE "${_TC_SOKOL_SHDC}")
            if(_TC_ATTEMPT LESS 3)
                message(STATUS "[${_TC_TARGET}] sokol-shdc download failed (${_TC_DOWNLOAD_STATUS}), retrying in 5s (attempt ${_TC_ATTEMPT}/3)...")
                execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 5)
            endif()
        endforeach()
        if(_TC_DOWNLOAD_ERROR)
            message(FATAL_ERROR "Failed to download sokol-shdc: ${_TC_DOWNLOAD_STATUS}")
        endif()
        # Make executable on Unix
        if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            file(CHMOD "${_TC_SOKOL_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        endif()
        message(STATUS "[${_TC_TARGET}] sokol-shdc downloaded successfully")
    endif()

    # Output languages: Metal (macOS/iOS/iOS-simulator), HLSL (Windows),
    # GLSL 4.30 (Linux desktop, SOKOL_GLCORE), GLSL ES3 (Android/RasPi/
    # WebGL2), WGSL (Web/WebGPU). metal_sim keeps apps runnable on the
    # iOS simulator (sg_query_backend() reports SG_BACKEND_METAL_SIMULATOR
    # there; without it shader lookup returns null sources and crashes in
    # sg_make_shader).
    # glsl430 is required for GLCORE — without it every custom shader on
    # Linux desktop fails with "Failed to get shader desc" (issue #193).
    set(_TC_SOKOL_SLANG "metal_macos:metal_ios:metal_sim:hlsl5:glsl430:glsl300es:wgsl")

    if(_TC_SHADERS_OUTPUT_DIR)
        file(MAKE_DIRECTORY "${_TC_SHADERS_OUTPUT_DIR}")
    endif()

    # Under Ninja, sokol-shdc writes into this build tree first and the header
    # is copied to its place only when its content changed. The headers sit
    # in source folders that every app shares (core, addons), and Ninja keeps
    # its build log per build tree: a header rewritten with the same content
    # by one app's first build would make the others rebuild (and relink a
    # running hot reload host). Ninja's restat keeps an unchanged header from
    # rebuilding anything. Other generators compare time stamps and write the
    # header in place as before.
    set(_TC_SHADER_TMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/tc_shaders/${_TC_TARGET}")

    set(_TC_SHADER_OUTPUTS "")
    foreach(_shader_src ${_TC_SHADER_SOURCES})
        get_filename_component(_shader_name ${_shader_src} NAME)
        if(_TC_SHADERS_OUTPUT_DIR)
            set(_shader_out "${_TC_SHADERS_OUTPUT_DIR}/${_shader_name}.h")
        else()
            set(_shader_out "${_shader_src}.h")
        endif()
        list(APPEND _TC_SHADER_OUTPUTS ${_shader_out})
        get_filename_component(_shader_out_dir "${_shader_out}" DIRECTORY)

        # -o is the file name only, from the folder it is written to:
        # sokol-shdc puts its command line in the header, which then reads
        # the same in every build tree and with every generator.
        if(CMAKE_GENERATOR MATCHES "Ninja")
            file(RELATIVE_PATH _shader_rel "${_TC_SOURCE_DIR}" "${_shader_src}")
            get_filename_component(_shader_rel_dir "${_shader_rel}" DIRECTORY)
            set(_shader_tmp_dir "${_TC_SHADER_TMP_DIR}/${_shader_rel_dir}")
            file(MAKE_DIRECTORY "${_shader_tmp_dir}")
            add_custom_command(
                OUTPUT ${_shader_out}
                BYPRODUCTS "${_shader_tmp_dir}/${_shader_name}.h"
                COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_name}.h -l ${_TC_SOKOL_SLANG} --ifdef
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_shader_name}.h ${_shader_out}
                WORKING_DIRECTORY ${_shader_tmp_dir}
                DEPENDS ${_shader_src}
                COMMENT "[${_TC_TARGET}] Compiling shader: ${_shader_name}"
            )
        else()
            add_custom_command(
                OUTPUT ${_shader_out}
                COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_name}.h -l ${_TC_SOKOL_SLANG} --ifdef
                WORKING_DIRECTORY ${_shader_out_dir}
                DEPENDS ${_shader_src}
                COMMENT "[${_TC_TARGET}] Compiling shader: ${_shader_name}"
            )
        endif()
    endforeach()

    add_custom_target(${_TC_TARGET}_shaders DEPENDS ${_TC_SHADER_OUTPUTS})
    add_dependencies(${_TC_TARGET} ${_TC_TARGET}_shaders)
    message(STATUS "[${_TC_TARGET}] Shader compilation enabled for ${_TC_SHADER_SOURCES}")
endfunction()
