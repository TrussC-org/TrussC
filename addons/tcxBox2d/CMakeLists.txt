# =============================================================================
# tcxBox2d - TrussC Box2D アドオン
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxBox2d)

# Box2D を FetchContent で取得（libs/ に展開して共有）
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)
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
FetchContent_MakeAvailable(box2d)

# ソースファイル
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${ADDON_NAME} PUBLIC box2d TrussC)
