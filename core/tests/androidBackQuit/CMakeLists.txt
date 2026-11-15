cmake_minimum_required(VERSION 3.20)
project(androidBackQuit CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(header "${CMAKE_CURRENT_SOURCE_DIR}/../../include/sokol/sokol_app_tc.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${header}" extract.py)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract.py"
    "${header}" "${CMAKE_CURRENT_BINARY_DIR}/android_functions.h"
    COMMAND_ERROR_IS_FATAL ANY)
add_executable(androidBackQuit main.cpp)
target_compile_features(androidBackQuit PRIVATE cxx_std_17)
target_include_directories(androidBackQuit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ../../include/sokol)
# Compile both NDK preprocessor branches; the executable exercises fallback
# and Choreographer with deterministic looper / Activity / EGL seams.
target_compile_definitions(androidBackQuit PRIVATE __ANDROID_API__=29)
add_library(androidBackQuitApi26 OBJECT main.cpp)
target_compile_features(androidBackQuitApi26 PRIVATE cxx_std_17)
target_include_directories(androidBackQuitApi26 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ../../include/sokol)
target_compile_definitions(androidBackQuitApi26 PRIVATE __ANDROID_API__=26)
if(MSVC)
    target_compile_options(androidBackQuit PRIVATE /utf-8)
    target_compile_options(androidBackQuitApi26 PRIVATE /utf-8)
endif()
