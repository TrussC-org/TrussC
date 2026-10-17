# Linux linker probes observe calls into the native platform layer, using real
# surfaceless Mesa rendering without requiring a window server.
option(TC_IMGUI_NATIVE_PROBE "Build the Linux native-state/deadline regression" OFF)
if(TC_IMGUI_NATIVE_PROBE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(TC_IMGUI_EGL EGL REQUIRED)
    add_executable(imguiNativeProbe probe/main.cpp)
    target_link_libraries(imguiNativeProbe PRIVATE tcxImGui ${TC_IMGUI_EGL})
    target_link_options(imguiNativeProbe PRIVATE
        "-Wl,--wrap=sapp_set_mouse_cursor,--wrap=sapp_get_mouse_cursor,--wrap=sapp_show_keyboard,--wrap=sapp_keyboard_shown")
    set_target_properties(imguiNativeProbe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endif()
