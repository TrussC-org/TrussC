# core/tests/trusscliPresets: compile trusscli's project-generation sources
# (tools/src) into this test, so it checks the real code that trusscli runs.
# tools/src/main.cpp (trusscli's own main) and the GUI (tcApp) are left out.
set(_TC_TRUSSCLI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/src")
target_sources(${PROJECT_NAME} PRIVATE
    "${_TC_TRUSSCLI_SRC}/ProjectGenerator.cpp"
    "${_TC_TRUSSCLI_SRC}/ProjectState.cpp"
    "${_TC_TRUSSCLI_SRC}/BuildSetup.cpp"
    "${_TC_TRUSSCLI_SRC}/IdeHelper.cpp"
    "${_TC_TRUSSCLI_SRC}/VsDetector.cpp"
)
target_include_directories(${PROJECT_NAME} PRIVATE "${_TC_TRUSSCLI_SRC}")
