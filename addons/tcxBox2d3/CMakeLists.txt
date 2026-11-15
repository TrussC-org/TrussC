cmake_minimum_required(VERSION 3.22)

include(FetchContent)
FetchContent_Declare(
    box2d3
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.2.0
)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_PROFILE OFF CACHE BOOL "" FORCE)
set(BOX2D_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/../../core/cmake/tc_fetch_quiet.cmake")
tc_fetch_quiet(box2d3)
# Upstream requires C17; leave its target name and compiler options intact.
set_property(TARGET box2d PROPERTY C_STANDARD 17)
set_property(TARGET box2d PROPERTY C_STANDARD_REQUIRED ON)

add_library(tcxBox2d3 STATIC src/tcxBox2dWorld.cpp)
target_include_directories(tcxBox2d3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tcxBox2d3 PUBLIC box2d TrussC)
