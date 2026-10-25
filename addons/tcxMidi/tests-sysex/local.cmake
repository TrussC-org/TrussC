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

# Compile the checked WinMM input callback on every host (see winmm.cpp).
FetchContent_GetProperties(libremidi SOURCE_DIR _sysex_libremidi)
set(SOURCE_FILE "${_sysex_libremidi}/include/libremidi/backends/winmm/midi_in.hpp")
set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/winmm_callback.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOURCE_FILE}")
include("${CMAKE_CURRENT_LIST_DIR}/ExtractWinmmCallback.cmake")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
