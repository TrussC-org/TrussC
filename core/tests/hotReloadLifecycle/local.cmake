# Observe native exit requests in the host fixture, without changing the core.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_C4_OBSERVE_SAPP_QUIT)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--wrap=sapp_quit")
endif()
