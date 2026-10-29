if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_LOCAL_TEXT_WRAP_NEW)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--wrap=_Znwm")
endif()
