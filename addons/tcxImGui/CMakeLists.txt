cmake_minimum_required(VERSION 3.20)
message(STATUS "[tcxImGui] Loading addon...")

# Platform-specific implementation file
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TCX_IMGUI_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/imgui_impl_ios.mm")
elseif(APPLE)
    set(TCX_IMGUI_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/imgui_impl_mac.mm")
elseif(ANDROID)
    set(TCX_IMGUI_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/imgui_impl_android.cpp")
elseif(EMSCRIPTEN)
    set(TCX_IMGUI_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/imgui_impl_web.cpp")
elseif(WIN32)
    set(TCX_IMGUI_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/imgui_impl_win.cpp")
else()
    set(TCX_IMGUI_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/imgui_impl_linux.cpp")
endif()

# Header files (for IDE visibility)
file(GLOB TCX_IMGUI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/imgui/*.h"
)

add_library(tcxImGui STATIC ${TCX_IMGUI_IMPL} ${TCX_IMGUI_HEADERS})
target_include_directories(tcxImGui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tcxImGui PUBLIC TrussC)

message(STATUS "[tcxImGui] Addon loaded")
