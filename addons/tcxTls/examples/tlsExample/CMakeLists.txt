# =============================================================================
# tlsExample - TLS (HTTPS) クライアントサンプル
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# プラットフォームに応じて言語を設定
if(APPLE)
    project(tlsExample LANGUAGES C CXX OBJC OBJCXX)
else()
    project(tlsExample LANGUAGES C CXX)
endif()

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# TrussC と tcxTls アドオンを追加
# =============================================================================
set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../trussc")
set(ADDON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

message(STATUS "[tlsExample] TrussC: ${TRUSSC_DIR}")
message(STATUS "[tlsExample] tcxTls: ${ADDON_DIR}")

add_subdirectory(${TRUSSC_DIR} ${CMAKE_BINARY_DIR}/TrussC)
add_subdirectory(${ADDON_DIR} ${CMAKE_BINARY_DIR}/tcxTls)

# =============================================================================
# 実行ファイル
# =============================================================================
add_executable(${PROJECT_NAME})

# ソースファイル
target_sources(${PROJECT_NAME} PRIVATE
    src/main.cpp
    src/tcApp.cpp
)

# TrussC と tcxTls をリンク
target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC tc::tcxTls)

# =============================================================================
# macOS 固有設定 (.app バンドル)
# =============================================================================
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )

    # アイコン設定（icns/にあれば使用、なければデフォルト）
    trussc_setup_icon(${PROJECT_NAME})

    # .app の出力先
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
else()
    # Windows / Linux
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endif()
