# Shadow libremidi only in the test executable. The addon's FetchContent target
# still builds with its real headers; no MIDI device is needed to run the test.
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mocks")

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
endif()
