# Observe actual std::thread creation without adding decoder state or changing
# the decoder's definition between translation units.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_HAP_WORKER_PROBE)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()
