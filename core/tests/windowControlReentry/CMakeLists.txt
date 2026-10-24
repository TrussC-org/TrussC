cmake_minimum_required(VERSION 3.20)
project(windowControlReentry LANGUAGES C CXX)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
add_executable(windowControlReentry main.cpp)
target_compile_features(windowControlReentry PRIVATE cxx_std_20)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(../.. "${CMAKE_CURRENT_SOURCE_DIR}/../../build-linux")
    target_link_libraries(windowControlReentry PRIVATE tc::TrussC)
    # Real dummy SG/SGL definitions and recording sapp endpoints precede the
    # native archive, as in shaderScopeDummy. No display server is involved.
    target_link_options(windowControlReentry PRIVATE -Wl,--allow-multiple-definition)
endif()
