# TrussC v0.0.1
# クロスプラットフォーム対応のクリエイティブコーディングフレームワーク

cmake_minimum_required(VERSION 3.20)
project(TrussC VERSION 0.0.1 LANGUAGES C CXX OBJC OBJCXX)

# C++20 を使用
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# sokol 実装を含む静的ライブラリ
if(TC_PLATFORM_MACOS)
    add_library(TrussC STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sokol_impl.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tcPlatform_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tcFbo_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tcVideoGrabber_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tcFileDialog_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pugixml_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl.mm
    )
elseif(TC_PLATFORM_WINDOWS)
    add_library(TrussC STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sokol_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tcFileDialog_win.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pugixml_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl.cpp
    )
elseif(TC_PLATFORM_LINUX)
    add_library(TrussC STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sokol_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tcFileDialog_linux.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pugixml_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl.cpp
    )
else()
    add_library(TrussC STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sokol_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pugixml_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_impl.cpp
    )
endif()

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
    target_compile_definitions(TrussC PUBLIC
        SOKOL_D3D11
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(TrussC PUBLIC
        d3d11
        dxgi
        dxguid
        comdlg32
        shell32
        ole32
    )

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
# アイコン設定ヘルパー関数
# =============================================================================
# trussc_setup_icon(<target>)
# - プロジェクトの icns/ フォルダに .icns があればそれを使用
# - なければ TrussC のデフォルトアイコンを使用
function(trussc_setup_icon TARGET_NAME)
    if(NOT APPLE)
        return()
    endif()

    # プロジェクトの icns/ フォルダを探す
    set(PROJECT_ICNS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/icns")
    file(GLOB PROJECT_ICNS_FILES "${PROJECT_ICNS_DIR}/*.icns")

    # TrussC のデフォルトアイコン
    set(DEFAULT_ICNS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/resources/default.icns")

    if(PROJECT_ICNS_FILES)
        # プロジェクト固有のアイコンを使用（最初の.icns）
        list(GET PROJECT_ICNS_FILES 0 APP_ICNS)
        get_filename_component(ICNS_NAME "${APP_ICNS}" NAME)
        message(STATUS "[${TARGET_NAME}] Using custom icon: ${ICNS_NAME}")
    elseif(EXISTS "${DEFAULT_ICNS}")
        # デフォルトアイコンを使用
        set(APP_ICNS "${DEFAULT_ICNS}")
        get_filename_component(ICNS_NAME "${APP_ICNS}" NAME)
        message(STATUS "[${TARGET_NAME}] Using default TrussC icon")
    else()
        # アイコンなし
        message(STATUS "[${TARGET_NAME}] No icon found")
        return()
    endif()

    # アイコンファイル名（パスなし）
    get_filename_component(ICNS_FILENAME "${APP_ICNS}" NAME)

    # バンドルにアイコンを設定
    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "${ICNS_FILENAME}"
    )

    # アイコンファイルをリソースとして追加
    set_source_files_properties("${APP_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${TARGET_NAME} PRIVATE "${APP_ICNS}")

    # ビルド後に quarantine 属性を削除（Finder からダブルクリックで開けるように）
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>"
        COMMENT "Removing quarantine attribute from ${TARGET_NAME}.app"
    )
endfunction()
