target_compile_definitions(TrussC PRIVATE SOKOL_TC_TEST_HOOKS)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=eglMakeCurrent)
endif()
