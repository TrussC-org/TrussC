# Standalone test of the Windows grabber's portable pixel conversion helper.
cmake_minimum_required(VERSION 3.16)
project(videoGrabberRGB32 CXX)

add_executable(videoGrabberRGB32 main.cpp)
target_compile_features(videoGrabberRGB32 PRIVATE cxx_std_20)
if(MSVC)
    # Sources and core headers are UTF-8; without /utf-8 MSVC reads them in the
    # console code page (CP932 etc.), and /WX turns the C4819 warning into an error (#793).
    target_compile_options(videoGrabberRGB32 PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(videoGrabberRGB32 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
enable_testing()
add_test(NAME videoGrabberRGB32 COMMAND videoGrabberRGB32)
