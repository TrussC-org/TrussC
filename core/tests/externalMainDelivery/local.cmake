# Keep the mock MIDI and TLS headers private to this fixture in allCoreTests.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    APPEND PROPERTY INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../addons/tcxMidi/tests/mocks"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../addons/tcxTls/src")
