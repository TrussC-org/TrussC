# =============================================================================
# TrussC プロジェクト
# =============================================================================
# 使い方:
#   1. projectGenerator でプロジェクトを作成/更新
#   2. mkdir build && cd build && cmake .. && cmake --build .
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# プロジェクト名（フォルダ名から自動取得）
get_filename_component(PROJECT_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_DIR_NAME} LANGUAGES C CXX OBJC OBJCXX)

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Xcode 用: 不要なターゲットを抑制
set(CMAKE_SUPPRESS_REGENERATION true)
# 自動スキーム生成を無効化（後でプロジェクトターゲットだけ有効にする）
set(CMAKE_XCODE_GENERATE_SCHEME OFF)

# =============================================================================
# TrussC パス設定
# =============================================================================
# projectGenerator が TC_ROOT を直接設定する
set(TC_ROOT "" CACHE PATH "Path to TrussC (e.g. /path/to/tc_v0.0.1)")

if(NOT TC_ROOT)
    message("")
    message("╔══════════════════════════════════════════════════════════════════╗")
    message("║                                                                  ║")
    message("║   ERROR: TC_ROOT is not set!                                    ║")
    message("║                                                                  ║")
    message("║   Use projectGenerator to create or update this project.        ║")
    message("║                                                                  ║")
    message("╚══════════════════════════════════════════════════════════════════╝")
    message("")
    message(FATAL_ERROR "Configuration failed - TC_ROOT not set")
endif()

# パスが存在するか確認
if(NOT EXISTS "${TC_ROOT}/CMakeLists.txt")
    message("")
    message("╔══════════════════════════════════════════════════════════════════╗")
    message("║                                                                  ║")
    message("║   ERROR: TrussC not found!                                      ║")
    message("║                                                                  ║")
    message("║   Looked in: ${TC_ROOT}")
    message("║                                                                  ║")
    message("║   Use projectGenerator to update this project.                  ║")
    message("║                                                                  ║")
    message("╚══════════════════════════════════════════════════════════════════╝")
    message("")
    message(FATAL_ERROR "Configuration failed - TrussC not found at ${TC_ROOT}")
endif()

message(STATUS "[${PROJECT_NAME}] TrussC: ${TC_ROOT}")

# TrussC 自体のアドオンはビルドしない（ユーザーが addons.make で指定したものだけ）
set(TC_BUILD_ADDONS OFF CACHE BOOL "" FORCE)
# アドオンの examples もビルドしない（ターゲットをシンプルに保つ）
set(TCX_BOX2D_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# アドオンの examples はビルドしない（ターゲットをシンプルに保つ）
set(TCX_BOX2D_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# TrussC を追加
add_subdirectory(${TC_ROOT} ${CMAKE_BINARY_DIR}/TrussC)

# use_addon マクロを読み込み（アドオン用）
include(${TC_ROOT}/cmake/use_addon.cmake)

# =============================================================================
# 実行ファイル
# =============================================================================
add_executable(${PROJECT_NAME})

# ソースファイル
target_sources(${PROJECT_NAME} PRIVATE
    src/main.cpp
    src/tcApp.cpp
)

# TrussC をリンク
target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

# addons.make からアドオンを読み込み
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/addons.make")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/addons.make" _ADDON_LIST)
    foreach(_ADDON ${_ADDON_LIST})
        # 空行とコメント行をスキップ
        string(STRIP "${_ADDON}" _ADDON)
        if(_ADDON AND NOT _ADDON MATCHES "^#")
            use_addon(${PROJECT_NAME} ${_ADDON})
        endif()
    endforeach()
endif()

# =============================================================================
# 出力設定
# =============================================================================
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        # Xcode: このターゲットだけスキームを生成
        XCODE_GENERATE_SCHEME TRUE
    )
    # アイコン設定（icns/にあれば使用、なければデフォルト）
    trussc_setup_icon(${PROJECT_NAME})
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endif()
