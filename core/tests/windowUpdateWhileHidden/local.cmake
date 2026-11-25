# Exercise the actual Android entry on desktop with the real CPU scheduler.
set(_runtime "${TRUSSC_DIR}/include/tc/app/tcWindowRuntime.cpp")
configure_file("${_runtime}" "${CMAKE_CURRENT_BINARY_DIR}/background-runtime.txt" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/extract-background.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/extract-background.cmake" COPYONLY)
set(_extracted "${CMAKE_CURRENT_BINARY_DIR}/background-entry.cpp")
# A child avoids expanding the entire Runtime into CMake's recipe trace.
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/background-runtime.txt"
    "-DOUTPUT=${_extracted}" -P "${CMAKE_CURRENT_BINARY_DIR}/extract-background.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
target_sources(${PROJECT_NAME} PRIVATE "${_extracted}")
