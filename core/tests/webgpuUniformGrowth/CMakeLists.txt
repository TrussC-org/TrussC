# Standalone implementation: never link libTrussC's sokol implementation.
cmake_minimum_required(VERSION 3.16)
project(webgpuUniformGrowth LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The native build runs the mock whenever emdawn's WebGPU headers are found
# (an emsdk that has built a WebGPU target once), so the native sweep tests the
# backend instead of printing a skip. The mock relies on GNU-style section GC.
if(NOT EMSCRIPTEN AND NOT WEBGPU_INCLUDE_DIR AND NOT APPLE AND NOT MSVC)
    set(_tc_emdawn_suffix ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include)
    find_path(WEBGPU_INCLUDE_DIR webgpu/webgpu.h
        PATHS "$ENV{EM_CACHE}/${_tc_emdawn_suffix}"
              "$ENV{EMSDK}/upstream/emscripten/cache/${_tc_emdawn_suffix}"
              "$ENV{HOME}/emsdk/upstream/emscripten/cache/${_tc_emdawn_suffix}"
        NO_DEFAULT_PATH)
endif()
if(WEBGPU_INCLUDE_DIR AND NOT EMSCRIPTEN)
    message(STATUS "webgpuUniformGrowth: native mock with ${WEBGPU_INCLUDE_DIR}")
    add_executable(webgpuUniformGrowth mock.cpp)
    target_include_directories(webgpuUniformGrowth PRIVATE "${WEBGPU_INCLUDE_DIR}")
    target_compile_options(webgpuUniformGrowth PRIVATE -ffunction-sections -fdata-sections -Wno-unused-function)
    target_link_options(webgpuUniformGrowth PRIVATE -Wl,--gc-sections)
else()
    if(NOT EMSCRIPTEN)
        message(STATUS "webgpuUniformGrowth: emdawn WebGPU headers not found; native build only prints a skip")
    endif()
    add_executable(webgpuUniformGrowth main.cpp)
endif()
set(SOKOL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include/sokol" CACHE PATH "Sokol headers (baseline comparison)")
target_include_directories(webgpuUniformGrowth PRIVATE "${SOKOL_INCLUDE_DIR}")
if(TC_WEBGPU_BASELINE)
    target_compile_definitions(webgpuUniformGrowth PRIVATE TC_WEBGPU_BASELINE)
endif()
if(EMSCRIPTEN)
    target_compile_options(webgpuUniformGrowth PRIVATE --use-port=emdawnwebgpu)
    target_link_options(webgpuUniformGrowth PRIVATE --use-port=emdawnwebgpu
        -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 -sGROWABLE_ARRAYBUFFERS=0)
    set_target_properties(webgpuUniformGrowth PROPERTIES SUFFIX ".js")
    configure_file(index.html index.html COPYONLY)
elseif(NOT MSVC)
    target_compile_options(webgpuUniformGrowth PRIVATE -Wall -Wextra)
else()
    target_compile_options(webgpuUniformGrowth PRIVATE /utf-8)
endif()
