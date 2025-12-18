# =============================================================================
# TrussC サンプルプロジェクト
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# プロジェクト名（フォルダ名から自動取得）
get_filename_component(PROJECT_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)

# プラットフォームに応じて言語を設定（OBJC/OBJCXX は macOS のみ）
if(APPLE)
    project(${PROJECT_DIR_NAME} LANGUAGES C CXX OBJC OBJCXX)
else()
    project(${PROJECT_DIR_NAME} LANGUAGES C CXX)
endif()

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TrussC パス（相対パス）
set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
message(STATUS "[${PROJECT_DIR_NAME}] TrussC: ${TRUSSC_DIR}")

# TrussC を追加
add_subdirectory(${TRUSSC_DIR} ${CMAKE_BINARY_DIR}/TrussC)

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
    trussc_setup_icon(${PROJECT_NAME})
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endif()
