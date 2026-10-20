cmake_minimum_required(VERSION 3.20)
project(shaderScopeDummy LANGUAGES C CXX)
add_executable(shaderScopeDummy main.cpp)
target_compile_features(shaderScopeDummy PRIVATE cxx_std_20)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(shaderScopeDummy PRIVATE -UNDEBUG)
    # Preserve the reviewer's real-Shader/Fbo dummy probe. The dummy symbols
    # precede the native archive; only the windowless probe uses this override.
    # A standalone unit test has no src/ and does not use trussc_app().
    add_subdirectory(../.. "${CMAKE_CURRENT_SOURCE_DIR}/../../build-linux")
    target_link_libraries(shaderScopeDummy PRIVATE tc::TrussC)
    target_link_options(shaderScopeDummy PRIVATE -Wl,--allow-multiple-definition)
    # This consumer always has assertions, even when the shared core archive
    # was built with NDEBUG (the usual RelWithDebInfo build).
    add_executable(pipelineOwnerLinkCheck pipelineOwnerLinkCheck.cpp)
    target_compile_features(pipelineOwnerLinkCheck PRIVATE cxx_std_20)
    target_link_libraries(pipelineOwnerLinkCheck PRIVATE tc::TrussC)
endif()

option(TC_DUMMY_SANITIZERS "Instrument the real coordinator and dummy regressions" OFF)
if(TC_DUMMY_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Put the instrumented coordinator before the native archive, just like the
    # review probe, so both sides of teardown bookkeeping are checked.
    target_sources(shaderScopeDummy PRIVATE ../../include/tc/app/tcGlobal.cpp)
    target_compile_options(shaderScopeDummy PRIVATE -UNDEBUG -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(shaderScopeDummy PRIVATE -fsanitize=address,undefined)
endif()
