# =============================================================================
# oscExample - tcxOsc サンプルプロジェクト
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# プロジェクト名
project(oscExample LANGUAGES C CXX OBJC OBJCXX)

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# TrussC パス設定
# =============================================================================
# TC_ROOT を自動検出（このファイルから相対パス）
get_filename_component(TC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

if(NOT EXISTS "${TC_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "TrussC not found at ${TC_ROOT}")
endif()

message(STATUS "[${PROJECT_NAME}] TrussC: ${TC_ROOT}")

# アドオンの examples はビルドしない
set(TC_BUILD_ADDONS OFF CACHE BOOL "" FORCE)
set(TCX_BOX2D_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TCX_OSC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# TrussC を追加
add_subdirectory(${TC_ROOT} ${CMAKE_BINARY_DIR}/TrussC)

# tcxOsc アドオンを追加
add_subdirectory(${TC_ROOT}/addons/tcxOsc ${CMAKE_BINARY_DIR}/tcxOsc)

# =============================================================================
# 実行ファイル
# =============================================================================
set(EXAMPLE_SOURCES
    src/main.cpp
    src/tcApp.cpp
)

if(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${EXAMPLE_SOURCES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
else()
    add_executable(${PROJECT_NAME} ${EXAMPLE_SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    tcxOsc
)

# アイコン設定
trussc_setup_icon(${PROJECT_NAME})
