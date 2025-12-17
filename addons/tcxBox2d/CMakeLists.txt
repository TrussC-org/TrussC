# =============================================================================
# tcxBox2d - TrussC Box2D アドオン
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# アドオン名
set(ADDON_NAME tcxBox2d)

# =============================================================================
# Box2D を FetchContent で取得
# =============================================================================
include(FetchContent)

FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v2.4.1
)

# Box2D のビルドオプション
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_USER_SETTINGS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(box2d)

# =============================================================================
# アドオンライブラリ
# =============================================================================
set(ADDON_SOURCES
    src/tcxBox2dWorld.cpp
    src/tcxBox2dCircle.cpp
    src/tcxBox2dRect.cpp
    src/tcxBox2dPolygon.cpp
)

set(ADDON_HEADERS
    include/tcxBox2d/tcxBox2d.h
    include/tcxBox2d/tcxBox2dWorld.h
    include/tcxBox2d/tcxBox2dBody.h
    include/tcxBox2d/tcxBox2dCircle.h
    include/tcxBox2d/tcxBox2dRect.h
    include/tcxBox2d/tcxBox2dPolygon.h
)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${ADDON_NAME} PUBLIC
    box2d
    TrussC
)

# =============================================================================
# サンプルプロジェクト
# =============================================================================
option(TCX_BOX2D_BUILD_EXAMPLES "Build tcxBox2d examples" ON)

if(TCX_BOX2D_BUILD_EXAMPLES)
    add_subdirectory(examples/box2dBasicExample)
    add_subdirectory(examples/box2dNodeExample)
endif()
