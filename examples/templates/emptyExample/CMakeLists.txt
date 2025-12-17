# =============================================================================
# TrussC プロジェクトテンプレート
# =============================================================================
# 使い方:
#   1. このフォルダを好きな場所にコピー
#   2. 環境変数 TC_PATH を TrussC の親ディレクトリに設定
#   3. mkdir build && cd build && cmake .. && cmake --build .
#
# バージョン管理:
#   TC_PATH=/path/to/TrussC
#          ├── tc_v0.0.1/
#          ├── tc_v0.1.0/
#          └── tc_v1.0.0/
#
#   下の TC_VERSION を変更してバージョンを切り替え
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
# projectGenerator が生成時に TC_ROOT を直接設定する
# 手動でテンプレートをコピーした場合は環境変数 TC_PATH を使用
set(TC_ROOT "" CACHE PATH "Path to TrussC (e.g. /path/to/tc_v0.0.1)")

if(NOT TC_ROOT)
    # 環境変数からフォールバック
    set(TC_VERSION "0.0.1")
    if(DEFINED ENV{TC_PATH})
        set(TC_ROOT "$ENV{TC_PATH}/tc_v${TC_VERSION}")
    else()
        message("")
        message("╔══════════════════════════════════════════════════════════════════╗")
        message("║                                                                  ║")
        message("║   ERROR: TC_ROOT is not set!                                    ║")
        message("║                                                                  ║")
        message("╠══════════════════════════════════════════════════════════════════╣")
        message("║                                                                  ║")
        message("║   Option 1: Use projectGenerator to create/update project       ║")
        message("║                                                                  ║")
        message("║   Option 2: Set TC_PATH environment variable:                   ║")
        message("║     export TC_PATH=/path/to/TrussC                              ║")
        message("║                                                                  ║")
        message("╚══════════════════════════════════════════════════════════════════╝")
        message("")
        message(FATAL_ERROR "Configuration failed - TC_ROOT not set")
    endif()
endif()

# パスが存在するか確認
if(NOT EXISTS "${TC_ROOT}/CMakeLists.txt")
    message("")
    message("╔══════════════════════════════════════════════════════════════════╗")
    message("║                                                                  ║")
    message("║   ERROR: TrussC v${TC_VERSION} not found!")
    message("║                                                                  ║")
    message("╠══════════════════════════════════════════════════════════════════╣")
    message("║                                                                  ║")
    message("║   Looked in: ${TC_ROOT}")
    message("║                                                                  ║")
    message("║   Available versions in ${TC_PATH}:")
    file(GLOB _TC_VERSIONS LIST_DIRECTORIES true "${TC_PATH}/tc_v*")
    foreach(_V ${_TC_VERSIONS})
        get_filename_component(_VNAME ${_V} NAME)
        message("║     - ${_VNAME}")
    endforeach()
    if(NOT _TC_VERSIONS)
        message("║     (none found)")
    endif()
    message("║                                                                  ║")
    message("║   Check TC_VERSION in CMakeLists.txt                            ║")
    message("║                                                                  ║")
    message("╚══════════════════════════════════════════════════════════════════╝")
    message("")
    message(FATAL_ERROR "Configuration failed - TrussC v${TC_VERSION} not found")
endif()

message(STATUS "[${PROJECT_NAME}] TrussC v${TC_VERSION}: ${TC_ROOT}")

# TrussC 自体のアドオンはビルドしない（ユーザーが addons.make で指定したものだけ）
set(TC_BUILD_ADDONS OFF CACHE BOOL "" FORCE)
# アドオンの examples もビルドしない（ターゲットをシンプルに保つ）
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
