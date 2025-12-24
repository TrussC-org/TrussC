# =============================================================================
# trussc_app.cmake - TrussC application setup macro
# =============================================================================
#
# Usage:
#   set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
#   include(${TRUSSC_DIR}/cmake/trussc_app.cmake)
#   trussc_app()
#
# This is all you need to build a TrussC app.
# To use addons, list addon names in addons.make file.
#
# Options:
#   trussc_app(SOURCES file1.cpp file2.cpp)  # Explicit source files
#   trussc_app(NAME myapp)                    # Explicit project name
#
# Shader compilation:
#   If src/**/*.glsl files exist, they are automatically compiled
#   with sokol-shdc to generate cross-platform shader headers.
#
# =============================================================================

macro(trussc_app)
    # デフォルトビルドタイプを RelWithDebInfo に設定
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()

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

    # ソースファイル（指定がなければ src/ から再帰的に自動収集）
    if(_TC_APP_SOURCES)
        set(_TC_SOURCES ${_TC_APP_SOURCES})
    else()
        file(GLOB_RECURSE _TC_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
        )
    endif()

    # Xcode / Visual Studio でディレクトリ構造を維持
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${_TC_SOURCES})

    # 実行ファイル作成
    add_executable(${PROJECT_NAME} ${_TC_SOURCES})

    # TrussC リンク
    target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

    # Apply addons from addons.make
    apply_addons(${PROJECT_NAME})

    # Compile shaders with sokol-shdc (if any .glsl files exist)
    file(GLOB_RECURSE _TC_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.glsl")
    if(_TC_SHADER_SOURCES)
        # Select sokol-shdc binary based on host platform
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
            if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
                set(_TC_SOKOL_SHDC "${TC_ROOT}/tools/sokol-shdc/sokol-shdc-osx_arm64")
            else()
                set(_TC_SOKOL_SHDC "${TC_ROOT}/tools/sokol-shdc/sokol-shdc-osx_x64")
            endif()
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(_TC_SOKOL_SHDC "${TC_ROOT}/tools/sokol-shdc/sokol-shdc-win32.exe")
        else()
            set(_TC_SOKOL_SHDC "${TC_ROOT}/tools/sokol-shdc/sokol-shdc-linux")
        endif()

        # Output languages: Metal macOS, GLES3 for Web, WGSL for WebGPU
        set(_TC_SOKOL_SLANG "metal_macos:glsl300es:wgsl")

        set(_TC_SHADER_OUTPUTS "")
        foreach(_shader_src ${_TC_SHADER_SOURCES})
            set(_shader_out "${_shader_src}.h")
            list(APPEND _TC_SHADER_OUTPUTS ${_shader_out})

            get_filename_component(_shader_name ${_shader_src} NAME)
            add_custom_command(
                OUTPUT ${_shader_out}
                COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_out} -l ${_TC_SOKOL_SLANG} --ifdef
                DEPENDS ${_shader_src}
                COMMENT "Compiling shader: ${_shader_name}"
            )
        endforeach()

        add_custom_target(${PROJECT_NAME}_shaders DEPENDS ${_TC_SHADER_OUTPUTS})
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_shaders)
        message(STATUS "[${PROJECT_NAME}] Shader compilation enabled for ${_TC_SHADER_SOURCES}")
    endif()

    # Output settings
    if(EMSCRIPTEN)
        # Emscripten: HTML 出力
        set_target_properties(${PROJECT_NAME} PROPERTIES
            SUFFIX ".html"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        )
        # カスタムシェル HTML のパス
        set(_TC_SHELL_FILE "${TC_ROOT}/trussc/platform/web/shell.html")
        # WebGL2 リンクオプション
        target_link_options(${PROJECT_NAME} PRIVATE
            -sUSE_WEBGL2=1
            -sALLOW_MEMORY_GROWTH=1
            -sFULL_ES3=1
            --shell-file=${_TC_SHELL_FILE}
        )
        # bin/data フォルダが存在する場合は自動的にプリロード
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/data")
            target_link_options(${PROJECT_NAME} PRIVATE
                --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/bin/data@/data
            )
            message(STATUS "[${PROJECT_NAME}] Preloading data folder for Emscripten")
        endif()
    elseif(APPLE)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            # Xcode: スキームを生成してデフォルトターゲットにする
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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
