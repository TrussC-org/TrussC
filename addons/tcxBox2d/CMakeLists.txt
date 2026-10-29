# =============================================================================
# tcxBox2d - TrussC Box2D Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxBox2d)

# Fetch Box2D via FetchContent
include(FetchContent)
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v2.4.1
)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_USER_SETTINGS OFF CACHE BOOL "" FORCE)
# Upstream still declares a policy version older than 3.10. CMake 4 warns
# about that compatibility mode. Apply the documented external policy floor
# only while adding this dependency, preserving any higher caller setting.
function(_tcxBox2d_make_available)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND
       (NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS "3.10"))
        set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    endif()
    FetchContent_MakeAvailable(box2d)
endfunction()
_tcxBox2d_make_available()

# Source files
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${ADDON_NAME} PUBLIC box2d TrussC)
