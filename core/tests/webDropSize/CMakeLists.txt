cmake_minimum_required(VERSION 3.16)
project(webDropSize CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(app_header ${CMAKE_CURRENT_SOURCE_DIR}/../../include/sokol/sokol_app_tc.h
    CACHE FILEPATH "Application header under test")
add_executable(webDropSize main.cpp)
if(MSVC)
    # Sources and core headers are UTF-8, including on CP932 hosts (#793).
    target_compile_options(webDropSize PRIVATE /utf-8)
endif()
target_compile_definitions(webDropSize PRIVATE WEB_DROP_APP_HEADER="${app_header}")
option(WEB_DROP_LEGACY_HEADER "Run shared checks against a header without the 64-bit getter" OFF)
if(NOT WEB_DROP_LEGACY_HEADER)
    target_compile_definitions(webDropSize PRIVATE TEST_HAS_SIZE64=1)
endif()
if(EMSCRIPTEN)
    target_compile_definitions(webDropSize PRIVATE SOKOL_APP_TC_IMPL SOKOL_GLES3 SOKOL_NO_ENTRY)
    target_link_options(webDropSize PRIVATE "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1")
else()
    # Compile the Web C preflight on native hosts so sanitizers cover it too.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web_drop_preflight.inc
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_preflight.py
            ${app_header} ${CMAKE_CURRENT_BINARY_DIR}/web_drop_preflight.inc
        DEPENDS ${app_header} extract_preflight.py VERBATIM)
    target_sources(webDropSize PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/web_drop_preflight.inc)
    target_include_directories(webDropSize PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
