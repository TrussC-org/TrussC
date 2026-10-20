# The owned Linux test exhausts C allocators in framework code as well as new.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_TEST_WRAP_HEAP)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()
