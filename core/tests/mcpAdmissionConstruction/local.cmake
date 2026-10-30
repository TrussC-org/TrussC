# Headless production capture path with a modeled framebuffer on Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_ADMISSION_CAPTURE_PROBE)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=sapp_width -Wl,--wrap=sapp_height -Wl,--wrap=glReadPixels
        -Wl,--wrap=malloc -Wl,--wrap=recv -Wl,--wrap=sg_isvalid -Wl,--wrap=sg_tc_submit_prefix)
endif()
