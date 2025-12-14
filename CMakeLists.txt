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
    )
else()
    add_library(TrussC STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sokol_impl.cpp
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
    )

elseif(TC_PLATFORM_LINUX)
    target_compile_definitions(TrussC PUBLIC SOKOL_GLCORE)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(TrussC PUBLIC
        ${X11_LIBRARIES}
        ${X11_Xi_LIB}
        ${X11_Xcursor_LIB}
        ${OPENGL_LIBRARIES}
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
