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
if(EMSCRIPTEN)
    # Emscripten は UNIX=TRUE を設定するので、UNIX より前にチェック
    set(TC_PLATFORM_EMSCRIPTEN TRUE)
    set(TC_GRAPHICS_BACKEND "WGPU")  # WebGPU
    message(STATUS "[TrussC] Platform: Emscripten (WebGPU)")
elseif(APPLE)
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
file(GLOB TC_IMPL_SOURCES     ${CMAKE_CURRENT_SOURCE_DIR}/include/impl/*.cpp)
file(GLOB TC_APP_SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/include/tc/app/*.cpp)
file(GLOB TC_GRAPHICS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/tc/graphics/*.cpp)
file(GLOB TC_NETWORK_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/include/tc/network/*.cpp)
file(GLOB TC_SOUND_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/include/tc/sound/*.cpp)

# プラットフォーム固有ソースファイル
if(TC_PLATFORM_EMSCRIPTEN)
    file(GLOB TC_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.cpp)
elseif(TC_PLATFORM_MACOS)
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
    ${TC_APP_SOURCES}
    ${TC_GRAPHICS_SOURCES}
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
if(TC_PLATFORM_EMSCRIPTEN)
    target_compile_definitions(TrussC PUBLIC SOKOL_WGPU)
    # WebGPU port for headers (emdawnwebgpu provides webgpu/webgpu.h)
    target_compile_options(TrussC PUBLIC --use-port=emdawnwebgpu)
    # Emscripten リンクオプションはアプリ側で設定（trussc_app.cmake）

elseif(TC_PLATFORM_MACOS)
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
        # Media Foundation (VideoPlayer)
        mf
        mfplat
        mfuuid
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
    # FFmpeg for VideoPlayer
    pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libswscale libavutil)
    # GStreamer for AAC audio decoding
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-audio-1.0)
    target_include_directories(TrussC PUBLIC
        ${GTK3_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
    )
    target_link_libraries(TrussC PUBLIC
        ${X11_LIBRARIES}
        ${X11_Xi_LIB}
        ${X11_Xcursor_LIB}
        ${OPENGL_LIBRARIES}
        ${GTK3_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
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
# - なければデフォルトアイコンを使用
#
# 対応フォーマット:
#   macOS: .icns, .icon (macOS 26 Tahoe+, actool 必要)
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
    set(FOUND_ANY FALSE)

    # 1. .icns を探す（複数あっても最初の1つを使用）
    file(GLOB ICNS_FILES "${ICON_DIR}/*.icns")
    if(ICNS_FILES)
        list(GET ICNS_FILES 0 ICNS_FILE)
        get_filename_component(ICNS_NAME "${ICNS_FILE}" NAME)
        message(STATUS "[${TARGET_NAME}] Using icon: ${ICNS_NAME}")
        _trussc_apply_macos_icns(${TARGET_NAME} "${ICNS_FILE}")
        set(FOUND_ANY TRUE)
    endif()

    # 2. .icon フォルダを探す（macOS 26 Tahoe 用）
    file(GLOB ICON_FOLDERS "${ICON_DIR}/*.icon")
    if(ICON_FOLDERS)
        list(GET ICON_FOLDERS 0 ICON_FOLDER)
        get_filename_component(ICON_FOLDER_NAME "${ICON_FOLDER}" NAME)
        _trussc_apply_macos_icon_folder(${TARGET_NAME} "${ICON_FOLDER}" SUCCESS)
        if(SUCCESS)
            message(STATUS "[${TARGET_NAME}] Using Tahoe icon: ${ICON_FOLDER_NAME}")
            set(FOUND_ANY TRUE)
        endif()
    endif()

    # 3. 何も見つからなければデフォルトを使用
    if(NOT FOUND_ANY)
        message(STATUS "[${TARGET_NAME}] Using default TrussC icon")
        # デフォルト .icns
        if(EXISTS "${DEFAULT_DIR}/default.icns")
            _trussc_apply_macos_icns(${TARGET_NAME} "${DEFAULT_DIR}/default.icns")
        endif()
        # デフォルト .icon (Tahoe 用)
        if(EXISTS "${DEFAULT_DIR}/default.icon")
            _trussc_apply_macos_icon_folder(${TARGET_NAME} "${DEFAULT_DIR}/default.icon" _DEFAULT_ICON_SUCCESS)
            if(_DEFAULT_ICON_SUCCESS)
                message(STATUS "[${TARGET_NAME}] Using default Tahoe icon")
            endif()
        endif()
    endif()

    # quarantine 属性を削除（最後に1回だけ）
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>"
        COMMENT "Removing quarantine attribute from ${TARGET_NAME}.app"
    )
endfunction()

# macOS .icns アイコン適用
function(_trussc_apply_macos_icns TARGET_NAME ICNS_FILE)
    get_filename_component(ICNS_FILENAME "${ICNS_FILE}" NAME)

    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "${ICNS_FILENAME}"
    )

    set_source_files_properties("${ICNS_FILE}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${TARGET_NAME} PRIVATE "${ICNS_FILE}")
endfunction()

# macOS .icon フォルダ適用 (macOS 26 Tahoe+, actool でコンパイル)
function(_trussc_apply_macos_icon_folder TARGET_NAME ICON_FOLDER OUT_SUCCESS)
    set(${OUT_SUCCESS} FALSE PARENT_SCOPE)

    # actool (Xcode に含まれる) を探す
    find_program(ACTOOL_EXECUTABLE actool
        HINTS /Applications/Xcode.app/Contents/Developer/usr/bin
              /Applications/Xcode-beta.app/Contents/Developer/usr/bin
    )
    if(NOT ACTOOL_EXECUTABLE)
        message(WARNING "[${TARGET_NAME}] actool not found - skipping .icon folder (install Xcode for macOS 26 Tahoe icon support)")
        return()
    endif()

    get_filename_component(ICON_NAME "${ICON_FOLDER}" NAME_WE)
    set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/icon_assets")
    set(ASSETS_CAR "${OUTPUT_DIR}/Assets.car")

    # actool で Assets.car にコンパイル
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    set(PARTIAL_PLIST "${OUTPUT_DIR}/partial-info.plist")
    execute_process(
        COMMAND ${ACTOOL_EXECUTABLE} "${ICON_FOLDER}"
            --compile "${OUTPUT_DIR}"
            --output-format human-readable-text
            --app-icon "${ICON_NAME}"
            --include-all-app-icons
            --target-device mac
            --platform macosx
            --minimum-deployment-target 14.0
            --output-partial-info-plist "${PARTIAL_PLIST}"
        RESULT_VARIABLE RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT RESULT EQUAL 0 OR NOT EXISTS "${ASSETS_CAR}")
        message(WARNING "[${TARGET_NAME}] Failed to compile .icon folder with actool")
        return()
    endif()

    # Assets.car を Resources に追加
    set_source_files_properties("${ASSETS_CAR}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${TARGET_NAME} PRIVATE "${ASSETS_CAR}")

    # Info.plist に CFBundleIconName を追加 (PlistBuddy で post-build)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND /usr/libexec/PlistBuddy -c "Delete :CFBundleIconName" "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Info.plist" 2>/dev/null || true
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleIconName string ${ICON_NAME}" "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Info.plist"
        COMMENT "Adding CFBundleIconName to Info.plist for Tahoe icon"
    )

    set(${OUT_SUCCESS} TRUE PARENT_SCOPE)
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

# =============================================================================
# Built-in Shaders (compiled with sokol-shdc)
# =============================================================================
file(GLOB TC_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
if(TC_SHADER_SOURCES)
    # Select sokol-shdc binary based on host platform
    set(_TC_SOKOL_SHDC_BASE_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(_TC_SOKOL_SHDC_DIR "osx_arm64")
        else()
            set(_TC_SOKOL_SHDC_DIR "osx")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_TC_SOKOL_SHDC_DIR "win32")
    else()
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(_TC_SOKOL_SHDC_DIR "linux_arm64")
        else()
            set(_TC_SOKOL_SHDC_DIR "linux")
        endif()
    endif()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_TC_SOKOL_SHDC_EXT ".exe")
    else()
        set(_TC_SOKOL_SHDC_EXT "")
    endif()
    set(_TC_SOKOL_SHDC_URL "${_TC_SOKOL_SHDC_BASE_URL}/${_TC_SOKOL_SHDC_DIR}/sokol-shdc${_TC_SOKOL_SHDC_EXT}")
    set(_TC_SOKOL_SHDC "${CMAKE_CURRENT_SOURCE_DIR}/tools/sokol-shdc/sokol-shdc${_TC_SOKOL_SHDC_EXT}")

    # Download sokol-shdc if not present
    if(NOT EXISTS "${_TC_SOKOL_SHDC}")
        message(STATUS "[TrussC] Downloading sokol-shdc...")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tools/sokol-shdc")
        file(DOWNLOAD "${_TC_SOKOL_SHDC_URL}" "${_TC_SOKOL_SHDC}"
            SHOW_PROGRESS
            STATUS _TC_DOWNLOAD_STATUS)
        list(GET _TC_DOWNLOAD_STATUS 0 _TC_DOWNLOAD_ERROR)
        if(_TC_DOWNLOAD_ERROR)
            message(FATAL_ERROR "Failed to download sokol-shdc: ${_TC_DOWNLOAD_STATUS}")
        endif()
        if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            file(CHMOD "${_TC_SOKOL_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        endif()
        message(STATUS "[TrussC] sokol-shdc downloaded successfully")
    endif()

    # Output directory for generated headers
    set(_TC_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/tc/gpu/shaders")
    file(MAKE_DIRECTORY "${_TC_SHADER_OUTPUT_DIR}")

    set(_TC_SOKOL_SLANG "metal_macos:hlsl5:glsl300es:wgsl")

    set(_TC_SHADER_OUTPUTS "")
    foreach(_shader_src ${TC_SHADER_SOURCES})
        get_filename_component(_shader_name ${_shader_src} NAME)
        set(_shader_out "${_TC_SHADER_OUTPUT_DIR}/${_shader_name}.h")
        list(APPEND _TC_SHADER_OUTPUTS ${_shader_out})

        add_custom_command(
            OUTPUT ${_shader_out}
            COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_out} -l ${_TC_SOKOL_SLANG} --ifdef
            DEPENDS ${_shader_src}
            COMMENT "[TrussC] Compiling shader: ${_shader_name}"
        )
    endforeach()

    add_custom_target(TrussC_shaders DEPENDS ${_TC_SHADER_OUTPUTS})
    add_dependencies(TrussC TrussC_shaders)
    message(STATUS "[TrussC] Built-in shader compilation enabled")
endif()

