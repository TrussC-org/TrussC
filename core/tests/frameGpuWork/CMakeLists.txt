# Standalone: the dummy backend cannot share libTrussC's sokol implementation.
cmake_minimum_required(VERSION 3.16)
project(frameGpuWork LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(frameGpuWork main.cpp predicate.c)
target_include_directories(frameGpuWork PRIVATE ../../include)
if(NOT MSVC)
    target_compile_options(frameGpuWork PRIVATE -Wall -Wextra -Wno-unused-function)
    target_link_libraries(frameGpuWork PRIVATE m)
else()
    # Sources and core headers are UTF-8; without /utf-8 MSVC reads them in the
    # console code page (CP932 etc.), and /WX turns the C4819 warning into an error (#793).
    target_compile_options(frameGpuWork PRIVATE /utf-8)
endif()
