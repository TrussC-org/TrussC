cmake_minimum_required(VERSION 3.16)
project(shaderStreamOverflow CXX)

# Like sglLayerUpload, this needs its own dummy-backend sokol implementation.
# It has a plain main() and cannot link the native-backend allCoreTests executable.
add_executable(shaderStreamOverflow main.cpp)
target_compile_features(shaderStreamOverflow PRIVATE cxx_std_17)
target_include_directories(shaderStreamOverflow PRIVATE ../../include)
if(NOT MSVC)
    target_compile_options(shaderStreamOverflow PRIVATE -Wall -Wextra -Wno-unused-function)
endif()
