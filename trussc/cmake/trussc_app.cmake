# =============================================================================
# trussc_app.cmake - TrussC アプリケーション設定マクロ
# =============================================================================
#
# 使い方:
#   set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
#   include(${TRUSSC_DIR}/cmake/trussc_app.cmake)
#   trussc_app()
#
# これだけで TrussC アプリがビルド可能になる。
# アドオンを使う場合は addons.make ファイルにアドオン名を記述する。
#
# オプション:
#   trussc_app(SOURCES file1.cpp file2.cpp)  # ソースを明示指定
#   trussc_app(NAME myapp)                    # プロジェクト名を明示指定
#
# =============================================================================

macro(trussc_app)
    # 引数をパース
    set(_options "")
    set(_oneValueArgs NAME)
    set(_multiValueArgs SOURCES)
    cmake_parse_arguments(_TC_APP "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    # プロジェクト名（指定がなければフォルダ名から取得）
    if(_TC_APP_NAME)
        set(_TC_PROJECT_NAME ${_TC_APP_NAME})
    else()
        get_filename_component(_TC_PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    endif()

    # プラットフォームに応じて言語を設定
    if(APPLE)
        project(${_TC_PROJECT_NAME} LANGUAGES C CXX OBJC OBJCXX)
    else()
        project(${_TC_PROJECT_NAME} LANGUAGES C CXX)
    endif()

    # C++20
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # TrussC を追加
    add_subdirectory(${TRUSSC_DIR} ${CMAKE_BINARY_DIR}/TrussC)

    # ソースファイル（指定がなければ src/ から自動収集）
    if(_TC_APP_SOURCES)
        set(_TC_SOURCES ${_TC_APP_SOURCES})
    else()
        file(GLOB _TC_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
        )
    endif()

    # 実行ファイル作成
    add_executable(${PROJECT_NAME} ${_TC_SOURCES})

    # TrussC リンク
    target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

    # addons.make からアドオン追加
    apply_addons(${PROJECT_NAME})

    # 出力設定
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
        # Visual Studio: スタートアッププロジェクトを設定
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
        # Windows: アイコン設定
        trussc_setup_icon(${PROJECT_NAME})
    endif()

    message(STATUS "[${PROJECT_NAME}] TrussC app configured")
endmacro()
