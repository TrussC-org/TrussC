# Separate executables keep the forced fallback out of native TrussC TUs.
cmake_minimum_required(VERSION 3.16)
project(atomicSharedPtr CXX)

find_package(Threads REQUIRED)
enable_testing()

foreach(test IN ITEMS atomicSharedPtr atomicSharedPtrNative)
    add_executable(${test} main.cpp)
    target_compile_features(${test} PRIVATE cxx_std_20)
    target_link_libraries(${test} PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(${test} PRIVATE /W4 /WX)
    else()
        target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 60)
endforeach()

# build_all.py runs the executable named after this directory, so CI always
# exercises the fallback, even on libraries with native atomic<shared_ptr>.
target_compile_definitions(atomicSharedPtr PRIVATE TC_TEST_FORCE_SHARED_PTR_FALLBACK)
