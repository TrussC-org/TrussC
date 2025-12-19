# TrussC v0.0.1
# クロスプラットフォーム対応のクリエイティブコーディングフレームワーク

cmake_minimum_required(VERSION 3.20)

# プラットフォームに応じて言語を設定（OBJC/OBJCXX は macOS のみ）
if(APPLE)
    project(TrussC VERSION 0.0.1 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(TrussC VERSION 0.0.1 LANGUAGES C CXX)
endif()

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TC_ROOT（trussc と addons を含むルートディレクトリ）
get_filename_component(TC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(TC_ROOT "${TC_ROOT}" CACHE PATH "TrussC root directory" FORCE)

# =============================================================================
# プラットフォーム検出
# =============================================================================
if(APPLE)
    set(TC_PLATFORM_MACOS TRUE)
    set(TC_GRAPHICS_BACKEND "METAL")
    message(STATUS "[TrussC] Platform: macOS (Metal)")
elseif(WIN32)
    set(TC_PLATFORM_WINDOWS TRUE)
    set(TC_GRAPHICS_BACKEND "D3D11")
    message(STATUS "[TrussC] Platform: Windows (D3D11)")
elseif(UNIX)
    if(EXISTS "/opt/vc/include/bcm_host.h")
        set(TC_PLATFORM_RASPBIAN TRUE)
        set(TC_GRAPHICS_BACKEND "GLES3")
        message(STATUS "[TrussC] Platform: Raspbian (GLES3)")
    else()
        set(TC_PLATFORM_LINUX TRUE)
        set(TC_GRAPHICS_BACKEND "GLCORE")
        message(STATUS "[TrussC] Platform: Linux (OpenGL Core)")
    endif()
endif()

# =============================================================================
# ライブラリ定義 (STATIC)
# =============================================================================
# 全てのヘッダーファイル（IDE でディレクトリ構造を表示するため）
file(GLOB_RECURSE TC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

# 共通ソースファイル（プラットフォーム非依存）
file(GLOB TC_IMPL_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/include/impl/*.cpp)
file(GLOB TC_NETWORK_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/tc/network/*.cpp)
file(GLOB TC_SOUND_SOURCES   ${CMAKE_CURRENT_SOURCE_DIR}/include/tc/sound/*.cpp)

# プラットフォーム固有ソースファイル
if(TC_PLATFORM_MACOS)
    file(GLOB TC_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/mac/*.mm)
elseif(TC_PLATFORM_WINDOWS)
    file(GLOB TC_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/win/*.cpp)
elseif(TC_PLATFORM_LINUX)
    file(GLOB TC_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/*.cpp)
else()
    file(GLOB TC_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/*.cpp)
endif()

# 全ソースをまとめる
set(TC_ALL_SOURCES
    ${TC_HEADERS}
    ${TC_IMPL_SOURCES}
    ${TC_NETWORK_SOURCES}
    ${TC_SOUND_SOURCES}
    ${TC_PLATFORM_SOURCES}
)

# Xcode / Visual Studio でディレクトリ構造を維持
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TC_ALL_SOURCES})

add_library(TrussC STATIC ${TC_ALL_SOURCES})

add_library(tc::TrussC ALIAS TrussC)

# インクルードパス
target_include_directories(TrussC PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sokol
)

# =============================================================================
# プラットフォーム固有の設定
# =============================================================================
if(TC_PLATFORM_MACOS)
    target_compile_definitions(TrussC PUBLIC SOKOL_METAL)
    target_link_libraries(TrussC PUBLIC
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
    )
    target_compile_options(TrussC PRIVATE -fobjc-arc)

elseif(TC_PLATFORM_WINDOWS)
    # WIN32_LEAN_AND_MEAN: windows.h の読み込みを軽量化
    # NOMINMAX: std::min / std::max との衝突を回避
    target_compile_definitions(TrussC PUBLIC
        SOKOL_D3D11
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _USE_MATH_DEFINES
    )
    target_link_libraries(TrussC PUBLIC
        d3d11
        dxgi
        dxguid
        comdlg32
        shell32
        ole32
    )
    # MSVC: ソースファイルをUTF-8として扱う
    # /wd4244, /wd4267: int/float, size_t/int 変換警告を無効化
    target_compile_options(TrussC PUBLIC /utf-8 /wd4244 /wd4267)

elseif(TC_PLATFORM_LINUX)
    target_compile_definitions(TrussC PUBLIC SOKOL_GLCORE)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(TrussC PUBLIC ${GTK3_INCLUDE_DIRS})
    target_link_libraries(TrussC PUBLIC
        ${X11_LIBRARIES}
        ${X11_Xi_LIB}
        ${X11_Xcursor_LIB}
        ${OPENGL_LIBRARIES}
        ${GTK3_LIBRARIES}
        pthread
        dl
    )

elseif(TC_PLATFORM_RASPBIAN)
    target_compile_definitions(TrussC PUBLIC SOKOL_GLES3)
    target_include_directories(TrussC PUBLIC /opt/vc/include)
    target_link_libraries(TrussC PUBLIC
        GLESv2
        EGL
        bcm_host
        pthread
    )
endif()

# =============================================================================
# アイコン設定ヘルパー関数（クロスプラットフォーム対応）
# =============================================================================
# trussc_setup_icon(<target>)
# - プロジェクトの icon/ フォルダを探す
# - プラットフォーム用のフォーマットがあればそれを使用
# - なければ PNG から変換を試みる
# - 失敗したらデフォルトアイコンを使用
#
# 対応フォーマット:
#   macOS: .icns (または .png から自動変換)
#   Windows: .ico (または .png から自動変換)
# =============================================================================

function(trussc_setup_icon TARGET_NAME)
    set(ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/icon")
    set(DEFAULT_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/resources")

    if(APPLE)
        _trussc_setup_icon_macos(${TARGET_NAME} "${ICON_DIR}" "${DEFAULT_DIR}")
    elseif(WIN32)
        _trussc_setup_icon_windows(${TARGET_NAME} "${ICON_DIR}" "${DEFAULT_DIR}")
    endif()
endfunction()

# -----------------------------------------------------------------------------
# macOS アイコン設定
# -----------------------------------------------------------------------------
function(_trussc_setup_icon_macos TARGET_NAME ICON_DIR DEFAULT_DIR)
    # 1. .icns を探す
    file(GLOB ICNS_FILES "${ICON_DIR}/*.icns")
    if(ICNS_FILES)
        list(GET ICNS_FILES 0 ICON_FILE)
        get_filename_component(ICON_NAME "${ICON_FILE}" NAME)
        message(STATUS "[${TARGET_NAME}] Using icon: ${ICON_NAME}")
        _trussc_apply_macos_icon(${TARGET_NAME} "${ICON_FILE}")
        return()
    endif()

    # 2. .png から変換を試みる
    file(GLOB PNG_FILES "${ICON_DIR}/*.png")
    if(PNG_FILES)
        list(GET PNG_FILES 0 PNG_FILE)
        get_filename_component(PNG_NAME "${PNG_FILE}" NAME_WE)
        set(OUTPUT_ICNS "${ICON_DIR}/${PNG_NAME}.icns")

        _trussc_convert_png_to_icns("${PNG_FILE}" "${OUTPUT_ICNS}" SUCCESS)
        if(SUCCESS)
            message(STATUS "[${TARGET_NAME}] Converted PNG to ICNS: ${PNG_NAME}.icns")
            _trussc_apply_macos_icon(${TARGET_NAME} "${OUTPUT_ICNS}")
            return()
        endif()
    endif()

    # 3. デフォルトを使用
    message(STATUS "[${TARGET_NAME}] Using default TrussC icon")
    _trussc_apply_macos_icon(${TARGET_NAME} "${DEFAULT_DIR}/default.icns")
endfunction()

# macOS アイコン適用
function(_trussc_apply_macos_icon TARGET_NAME ICON_FILE)
    get_filename_component(ICON_FILENAME "${ICON_FILE}" NAME)

    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "${ICON_FILENAME}"
    )

    set_source_files_properties("${ICON_FILE}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${TARGET_NAME} PRIVATE "${ICON_FILE}")

    # quarantine 属性を削除
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>"
        COMMENT "Removing quarantine attribute from ${TARGET_NAME}.app"
    )
endfunction()

# PNG → ICNS 変換 (macOS 標準ツール使用)
function(_trussc_convert_png_to_icns PNG_FILE OUTPUT_ICNS OUT_SUCCESS)
    set(${OUT_SUCCESS} FALSE PARENT_SCOPE)

    # sips と iconutil が必要
    find_program(SIPS_EXECUTABLE sips)
    find_program(ICONUTIL_EXECUTABLE iconutil)
    if(NOT SIPS_EXECUTABLE OR NOT ICONUTIL_EXECUTABLE)
        return()
    endif()

    # 一時ディレクトリ
    get_filename_component(ICON_DIR "${OUTPUT_ICNS}" DIRECTORY)
    set(ICONSET_DIR "${ICON_DIR}/_icon.iconset")

    # iconset を作成
    file(MAKE_DIRECTORY "${ICONSET_DIR}")

    # 各サイズを生成
    set(SIZES 16 32 128 256 512)
    foreach(SIZE ${SIZES})
        math(EXPR SIZE2 "${SIZE} * 2")
        execute_process(
            COMMAND ${SIPS_EXECUTABLE} -z ${SIZE} ${SIZE} "${PNG_FILE}" --out "${ICONSET_DIR}/icon_${SIZE}x${SIZE}.png"
            OUTPUT_QUIET ERROR_QUIET
        )
        execute_process(
            COMMAND ${SIPS_EXECUTABLE} -z ${SIZE2} ${SIZE2} "${PNG_FILE}" --out "${ICONSET_DIR}/icon_${SIZE}x${SIZE}@2x.png"
            OUTPUT_QUIET ERROR_QUIET
        )
    endforeach()

    # icns に変換
    execute_process(
        COMMAND ${ICONUTIL_EXECUTABLE} -c icns "${ICONSET_DIR}" -o "${OUTPUT_ICNS}"
        RESULT_VARIABLE RESULT
        OUTPUT_QUIET ERROR_QUIET
    )

    # 一時ディレクトリ削除
    file(REMOVE_RECURSE "${ICONSET_DIR}")

    if(RESULT EQUAL 0 AND EXISTS "${OUTPUT_ICNS}")
        set(${OUT_SUCCESS} TRUE PARENT_SCOPE)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Windows アイコン設定
# -----------------------------------------------------------------------------
function(_trussc_setup_icon_windows TARGET_NAME ICON_DIR DEFAULT_DIR)
    # 1. .ico を探す
    file(GLOB ICO_FILES "${ICON_DIR}/*.ico")
    if(ICO_FILES)
        list(GET ICO_FILES 0 ICON_FILE)
        get_filename_component(ICON_NAME "${ICON_FILE}" NAME)
        message(STATUS "[${TARGET_NAME}] Using icon: ${ICON_NAME}")
        _trussc_apply_windows_icon(${TARGET_NAME} "${ICON_FILE}")
        return()
    endif()

    # 2. .png から変換を試みる
    file(GLOB PNG_FILES "${ICON_DIR}/*.png")
    if(PNG_FILES)
        list(GET PNG_FILES 0 PNG_FILE)
        get_filename_component(PNG_NAME "${PNG_FILE}" NAME_WE)
        set(OUTPUT_ICO "${ICON_DIR}/${PNG_NAME}.ico")

        _trussc_convert_png_to_ico("${PNG_FILE}" "${OUTPUT_ICO}" SUCCESS)
        if(SUCCESS)
            message(STATUS "[${TARGET_NAME}] Converted PNG to ICO: ${PNG_NAME}.ico")
            _trussc_apply_windows_icon(${TARGET_NAME} "${OUTPUT_ICO}")
            return()
        endif()
    endif()

    # 3. デフォルトを使用
    message(STATUS "[${TARGET_NAME}] Using default TrussC icon")
    _trussc_apply_windows_icon(${TARGET_NAME} "${DEFAULT_DIR}/default.ico")
endfunction()

# Windows アイコン適用 (.rc ファイル経由)
function(_trussc_apply_windows_icon TARGET_NAME ICON_FILE)
    # .rc ファイルを生成
    get_filename_component(ICON_FILENAME "${ICON_FILE}" NAME)
    set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_icon.rc")

    file(WRITE "${RC_FILE}" "IDI_ICON1 ICON \"${ICON_FILE}\"\n")

    target_sources(${TARGET_NAME} PRIVATE "${RC_FILE}")
endfunction()

# PNG → ICO 変換 (ImageMagick 使用)
function(_trussc_convert_png_to_ico PNG_FILE OUTPUT_ICO OUT_SUCCESS)
    set(${OUT_SUCCESS} FALSE PARENT_SCOPE)

    # ImageMagick (magick または convert) が必要
    find_program(MAGICK_EXECUTABLE magick)
    if(NOT MAGICK_EXECUTABLE)
        find_program(MAGICK_EXECUTABLE convert)
    endif()
    if(NOT MAGICK_EXECUTABLE)
        return()
    endif()

    execute_process(
        COMMAND ${MAGICK_EXECUTABLE} "${PNG_FILE}" -define icon:auto-resize=256,128,64,48,32,16 "${OUTPUT_ICO}"
        RESULT_VARIABLE RESULT
        OUTPUT_QUIET ERROR_QUIET
    )

    if(RESULT EQUAL 0 AND EXISTS "${OUTPUT_ICO}")
        set(${OUT_SUCCESS} TRUE PARENT_SCOPE)
    endif()
endfunction()

# =============================================================================
# アドオンマクロを自動インクルード（use_addon が使えるようになる）
# =============================================================================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/use_addon.cmake)

