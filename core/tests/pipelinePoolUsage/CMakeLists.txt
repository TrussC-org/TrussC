cmake_minimum_required(VERSION 3.20)
project(pipelinePoolUsage LANGUAGES C CXX)
# Real coordinator with dummy sokol symbols, as in shaderScopeDummy.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
add_executable(pipelinePoolUsage main.cpp)
target_compile_features(pipelinePoolUsage PRIVATE cxx_std_20)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pipelinePoolUsage PRIVATE ../../include/tc/app/tcGlobal.cpp)
    target_compile_options(pipelinePoolUsage PRIVATE -UNDEBUG -O0)
    # Own core binary dir: sharing core/build-linux with shaderScopeDummy breaks
    # whichever project configured it last.
    add_subdirectory(../.. "${CMAKE_CURRENT_BINARY_DIR}/trussc-core")
    target_link_libraries(pipelinePoolUsage PRIVATE tc::TrussC)
    target_link_options(pipelinePoolUsage PRIVATE -Wl,--allow-multiple-definition)
endif()
