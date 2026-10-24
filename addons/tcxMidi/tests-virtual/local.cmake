if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # libremidi loads ALSA dynamically; the availability probe calls it directly.
    find_package(ALSA REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE ALSA::ALSA)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
endif()
