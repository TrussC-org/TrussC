# This test deliberately uses the real pinned decoder, never tests/mocks.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA QUIET)
    if(ALSA_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE ALSA::ALSA)
    endif()
endif()
