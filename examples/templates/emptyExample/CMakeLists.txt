# =============================================================================
# TrussC プロジェクトテンプレート
# =============================================================================
# 使い方:
#   1. このフォルダを好きな場所にコピー
#   2. 環境変数 TRUSSC_PATH を TrussC の親ディレクトリに設定
#   3. mkdir build && cd build && cmake .. && cmake --build .
#
# バージョン管理:
#   TRUSSC_PATH=/path/to/TrussC
#          ├── trussc_v0.0.1/
#          ├── trussc_v0.1.0/
#          └── trussc_v1.0.0/
#
#   下の TRUSSC_VERSION を変更してバージョンを切り替え
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# プロジェクト名（フォルダ名から自動取得）
get_filename_component(PROJECT_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_DIR_NAME} LANGUAGES C CXX OBJC OBJCXX)

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# TrussC バージョン指定
# =============================================================================
set(TRUSSC_VERSION "0.0.1" CACHE STRING "TrussC version to use")

# =============================================================================
# TrussC を探す
# =============================================================================
# 環境変数 TRUSSC_PATH から取得
if(DEFINED ENV{TRUSSC_PATH})
    set(TRUSSC_PATH "$ENV{TRUSSC_PATH}")
    set(TRUSSC_ROOT "${TRUSSC_PATH}/trussc_v${TRUSSC_VERSION}")
else()
    message("")
    message("╔══════════════════════════════════════════════════════════════════╗")
    message("║                                                                  ║")
    message("║   ERROR: TRUSSC_PATH environment variable is not set!           ║")
    message("║                                                                  ║")
    message("╠══════════════════════════════════════════════════════════════════╣")
    message("║                                                                  ║")
    message("║   Please set it to your TrussC parent directory:                ║")
    message("║                                                                  ║")
    message("║     export TRUSSC_PATH=/path/to/TrussC                          ║")
    message("║                                                                  ║")
    message("║   Expected structure:                                           ║")
    message("║     TRUSSC_PATH/                                                ║")
    message("║       ├── trussc_v0.0.1/                                        ║")
    message("║       ├── trussc_v0.1.0/                                        ║")
    message("║       └── ...                                                   ║")
    message("║                                                                  ║")
    message("║   Add to ~/.zshrc or ~/.bashrc for permanent setup.             ║")
    message("║                                                                  ║")
    message("╚══════════════════════════════════════════════════════════════════╝")
    message("")
    message(FATAL_ERROR "Configuration failed - TRUSSC_PATH not set")
endif()

# パスが存在するか確認
if(NOT EXISTS "${TRUSSC_ROOT}/CMakeLists.txt")
    message("")
    message("╔══════════════════════════════════════════════════════════════════╗")
    message("║                                                                  ║")
    message("║   ERROR: TrussC v${TRUSSC_VERSION} not found!")
    message("║                                                                  ║")
    message("╠══════════════════════════════════════════════════════════════════╣")
    message("║                                                                  ║")
    message("║   Looked in: ${TRUSSC_ROOT}")
    message("║                                                                  ║")
    message("║   Available versions in ${TRUSSC_PATH}:")
    file(GLOB _TRUSSC_VERSIONS LIST_DIRECTORIES true "${TRUSSC_PATH}/trussc_v*")
    foreach(_V ${_TRUSSC_VERSIONS})
        get_filename_component(_VNAME ${_V} NAME)
        message("║     - ${_VNAME}")
    endforeach()
    if(NOT _TRUSSC_VERSIONS)
        message("║     (none found)")
    endif()
    message("║                                                                  ║")
    message("║   Check TRUSSC_VERSION in CMakeLists.txt                        ║")
    message("║                                                                  ║")
    message("╚══════════════════════════════════════════════════════════════════╝")
    message("")
    message(FATAL_ERROR "Configuration failed - TrussC v${TRUSSC_VERSION} not found")
endif()

message(STATUS "[${PROJECT_NAME}] TrussC v${TRUSSC_VERSION}: ${TRUSSC_ROOT}")

# TrussC を追加
add_subdirectory(${TRUSSC_ROOT} ${CMAKE_BINARY_DIR}/TrussC)

# use_addon マクロを読み込み（アドオン用）
include(${TRUSSC_ROOT}/cmake/use_addon.cmake)

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

# アドオンを使う場合はここに追加
# use_addon(${PROJECT_NAME} tcxBox2d)

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
