# =============================================================================
# trussc_shaders.cmake - shader compilation with sokol-shdc
# =============================================================================
#
# Usage:
#   trussc_compile_shaders(<target> <source dir> [OUTPUT_DIR <dir>])
#
# Compiles every *.glsl under <source dir> (recursively) with sokol-shdc into
# <name>.glsl.h, next to the source, or in OUTPUT_DIR when given. The INTERFACE
# library <target>_shaders carries the output include directory and a dependency
# on <target>_shaders_gen, which builds the headers. Consumers link the library
# to inherit both. The library exists even when <source dir> has no *.glsl.
#
# Used by core (built-in shaders), trussc_app() (src/ of the app) and addons
# (e.g. tcxHap, tcxLut).
# =============================================================================

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/tc_acquire_shdc.cmake")

function(trussc_compile_shaders _TC_TARGET _TC_SOURCE_DIR)
    cmake_parse_arguments(_TC_SHADERS "" "OUTPUT_DIR" "" ${ARGN})

    add_library(${_TC_TARGET}_shaders INTERFACE)
    if(_TC_SHADERS_OUTPUT_DIR)
        set(_TC_SHADER_INCLUDE_DIR "${_TC_SHADERS_OUTPUT_DIR}")
    else()
        set(_TC_SHADER_INCLUDE_DIR "${_TC_SOURCE_DIR}")
    endif()
    target_include_directories(${_TC_TARGET}_shaders INTERFACE "${_TC_SHADER_INCLUDE_DIR}")

    # CONFIGURE_DEPENDS: an existing build tree re-globs on each build, so a
    # newly added shader gets its sokol-shdc command (and its header) without
    # a manual reconfigure (#366).
    file(GLOB_RECURSE _TC_SHADER_SOURCES CONFIGURE_DEPENDS "${_TC_SOURCE_DIR}/*.glsl")
    if(NOT _TC_SHADER_SOURCES)
        return()
    endif()

    # Select sokol-shdc binary based on host platform
    # sokol-tools-bin master HEAD when pinned (2026-08-29T14:15:01Z), including the
    # metal_sim and wgsl outputs used below. Update the commit and all five
    # hashes together; record the version in docs/LICENSE.md.
    set(_TC_SOKOL_SHDC_COMMIT "11d0cf678105d614d675e6d9bd2aaf3eeff12f8c")
    set(_TC_SOKOL_SHDC_SHA256_osx "8b4a6ac1172ec0d90dd41d611067d5e87a51e78dc28acb216cfc341d880b1d78")
    set(_TC_SOKOL_SHDC_SHA256_osx_arm64 "92db37975ad7ff3c3c9bc27cba1503287377cb287ebabf60d1c6b597abfa3244")
    set(_TC_SOKOL_SHDC_SHA256_linux "ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df")
    set(_TC_SOKOL_SHDC_SHA256_linux_arm64 "446b4bcea0c81d3ae529bc0d93533ea661b017f5b9ec2b2293a4c85f5fdcb639")
    set(_TC_SOKOL_SHDC_SHA256_win32 "bd616287f9ea689d53c6d260e443ee733e61ae1b73a9b37adc482ead0364d561")
    set(_TC_SOKOL_SHDC_BASE_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/${_TC_SOKOL_SHDC_COMMIT}/bin")
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
    set(_TC_SOKOL_SHDC_EXPECTED_HASH "${_TC_SOKOL_SHDC_SHA256_${_TC_SOKOL_SHDC_DIR}}")
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

    trussc_acquire_shdc("${_TC_SOKOL_SHDC}" "${_TC_SOKOL_SHDC_URL}"
        "${_TC_SOKOL_SHDC_EXPECTED_HASH}" "${_TC_SOKOL_SHDC_DIR}" "${_TC_SOKOL_SHDC_COMMIT}")

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
                DEPENDS ${_shader_src} "${_TC_SOKOL_SHDC}"
                COMMENT "[${_TC_TARGET}] Compiling shader: ${_shader_name}"
            )
        else()
            add_custom_command(
                OUTPUT ${_shader_out}
                COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_name}.h -l ${_TC_SOKOL_SLANG} --ifdef
                WORKING_DIRECTORY ${_shader_out_dir}
                DEPENDS ${_shader_src} "${_TC_SOKOL_SHDC}"
                COMMENT "[${_TC_TARGET}] Compiling shader: ${_shader_name}"
            )
        endif()
    endforeach()

    add_custom_target(${_TC_TARGET}_shaders_gen DEPENDS ${_TC_SHADER_OUTPUTS})
    add_dependencies(${_TC_TARGET}_shaders ${_TC_TARGET}_shaders_gen)
    set_property(TARGET ${_TC_TARGET}_shaders PROPERTY TC_SHADER_HEADERS ${_TC_SHADER_OUTPUTS})
    message(STATUS "[${_TC_TARGET}] Shader compilation enabled for ${_TC_SHADER_SOURCES}")
endfunction()

# trussc_shader_object_depends(<consumer> <name>_shaders ...)
# Ninja with an MSVC-style compiler records header dependencies relative to
# the build tree (../src/x.glsl.h), while a header generated outside the build
# tree is an absolute node in build.ninja. Ninja sees two files, so an edited
# shader regenerates its header but recompiles the consumer only on the next
# build. Naming the headers as OBJECT_DEPENDS puts the generated node itself
# in front of each of the consumer's objects. Other setups record the
# absolute path already and are left alone. It reads the consumer's SOURCES
# at call time, so call it after the target's sources are complete. Core,
# apps and the addon loader call it; an addon does not call it itself.
function(trussc_shader_object_depends _TC_CONSUMER)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja" OR NOT (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
        return()
    endif()
    set(_TC_HEADERS "")
    foreach(_TC_SHADERS IN LISTS ARGN)
        get_target_property(_TC_SHADER_HEADERS ${_TC_SHADERS} TC_SHADER_HEADERS)
        if(_TC_SHADER_HEADERS)
            list(APPEND _TC_HEADERS ${_TC_SHADER_HEADERS})
        endif()
    endforeach()
    if(NOT _TC_HEADERS)
        return()
    endif()
    get_target_property(_TC_SOURCES ${_TC_CONSUMER} SOURCES)
    get_target_property(_TC_SOURCE_DIR ${_TC_CONSUMER} SOURCE_DIR)
    foreach(_TC_SOURCE IN LISTS _TC_SOURCES)
        if(_TC_SOURCE MATCHES "\\.(c|cc|cpp|cxx)$" AND NOT _TC_SOURCE MATCHES "\\$<")
            # The loader calls this from the app's directory for an addon
            # target, so resolve relative sources against the target's own.
            if(NOT IS_ABSOLUTE "${_TC_SOURCE}")
                set(_TC_SOURCE "${_TC_SOURCE_DIR}/${_TC_SOURCE}")
            endif()
            set_property(SOURCE "${_TC_SOURCE}" TARGET_DIRECTORY ${_TC_CONSUMER}
                APPEND PROPERTY OBJECT_DEPENDS ${_TC_HEADERS})
        endif()
    endforeach()
endfunction()
