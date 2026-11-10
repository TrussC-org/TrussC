# Only this test needs Highway's target names for benchmark diagnostics.
if(MSVC)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "/external:I${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/highway")
else()
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "-isystem" "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/highway")
endif()
