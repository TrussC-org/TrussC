# Default mode models renderer validity/prefix and dimensions without a GPU/window. In both --gpu-check*
# modes the wrappers forward to real sokol for physical framebuffer validation.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_ADMISSION_CAPTURE_PROBE)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=_ZN6trussc8internal19windowCaptureSourceEv -Wl,--wrap=glReadPixels
        -Wl,--wrap=sg_isvalid -Wl,--wrap=sg_tc_submit_prefix)
endif()
