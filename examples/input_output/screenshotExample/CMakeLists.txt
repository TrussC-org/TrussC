# screenshotExample - FBO を使った画面キャプチャと画像保存

cmake_minimum_required(VERSION 3.20)
project(screenshotExample)

# C++20を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TrussC フレームワーク
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${CMAKE_CURRENT_BINARY_DIR}/TrussC)

# 実行ファイル
add_executable(screenshotExample MACOSX_BUNDLE
    src/main.cpp
    src/tcApp.cpp
)

target_link_libraries(screenshotExample PRIVATE TrussC)

# macOS 用設定
if(APPLE)
    set_target_properties(screenshotExample PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.screenshotExample"
        MACOSX_BUNDLE_BUNDLE_NAME "screenshotExample"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )

    # アイコン設定
    trussc_setup_icon(screenshotExample)

    # 出力先を bin フォルダに
    set_target_properties(screenshotExample PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin
    )
endif()
