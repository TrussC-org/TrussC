# Standalone test of the grabber's portable unique ID resolver.
cmake_minimum_required(VERSION 3.16)
project(grabberUniqueId CXX)

add_executable(grabberUniqueId main.cpp)
target_compile_features(grabberUniqueId PRIVATE cxx_std_20)
if(MSVC)
    # Sources and core headers are UTF-8; without /utf-8 MSVC reads them in the
    # console code page (CP932 etc.), and /WX turns the C4819 warning into an error (#793).
    target_compile_options(grabberUniqueId PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(grabberUniqueId PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
enable_testing()
add_test(NAME grabberUniqueId COMMAND grabberUniqueId)
