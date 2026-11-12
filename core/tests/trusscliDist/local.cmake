# core/tests/trusscliDist: compile trusscli's packaging logic (tools/src/Dist.cpp)
# into this test, so it checks the real code that `trusscli dist` runs.
set(_TC_TRUSSCLI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/src")
target_sources(${PROJECT_NAME} PRIVATE "${_TC_TRUSSCLI_SRC}/Dist.cpp")
# Keep the private tools headers on this fixture when built into allCoreTests.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    APPEND PROPERTY INCLUDE_DIRECTORIES "${_TC_TRUSSCLI_SRC}")
