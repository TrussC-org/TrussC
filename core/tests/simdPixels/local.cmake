# Only this test needs Highway's target names for benchmark diagnostics.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/highway")
