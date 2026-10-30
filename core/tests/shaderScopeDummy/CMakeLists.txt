cmake_minimum_required(VERSION 3.20)
project(shaderScopeDummy LANGUAGES C CXX)
# Keep the shared core archive in the fixture's existing native build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
add_executable(shaderScopeDummy main.cpp)
target_compile_features(shaderScopeDummy PRIVATE cxx_std_20)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(shaderScopeDummy PRIVATE ../../include/tc/app/tcGlobal.cpp)
    target_compile_options(shaderScopeDummy PRIVATE -UNDEBUG -O0)
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
    find_library(TC_SHADOW_EGL EGL)
    find_path(TC_SHADOW_EGL_INCLUDE EGL/egl.h)
    if(TC_SHADOW_EGL AND TC_SHADOW_EGL_INCLUDE)
        add_executable(shadowAllocationGl shadowAllocationGl.cpp)
        target_compile_features(shadowAllocationGl PRIVATE cxx_std_20)
        target_compile_definitions(shadowAllocationGl PRIVATE
            TC_CORE_SOKOL_ASSERTS=$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,0,1>)
        target_include_directories(shadowAllocationGl PRIVATE ${TC_SHADOW_EGL_INCLUDE})
        target_link_libraries(shadowAllocationGl PRIVATE tc::TrussC ${TC_SHADOW_EGL} ${CMAKE_DL_LIBS})
        target_compile_definitions(shaderScopeDummy PRIVATE TC_SHADOW_EGL_TEST)
    endif()
endif()

option(TC_DUMMY_SANITIZERS "Instrument the real coordinator and dummy regressions" OFF)
if(TC_DUMMY_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Instrument both the fixture and its real coordinator, before the native
    # archive, so both sides of teardown bookkeeping are checked.
    target_compile_options(shaderScopeDummy PRIVATE -UNDEBUG -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(shaderScopeDummy PRIVATE -fsanitize=address,undefined)
endif()
