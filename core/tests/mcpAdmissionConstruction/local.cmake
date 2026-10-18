# Default mode models sokol dimensions without a GPU/window. In both --gpu-check*
# modes the wrappers forward to real sokol for physical framebuffer validation.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_ADMISSION_CAPTURE_PROBE)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=sapp_width -Wl,--wrap=sapp_height -Wl,--wrap=glReadPixels)
endif()
