# core/tests/sglLayerUpload — standalone headless regression test.
#
# This is intentionally NOT a TrussC project: it compiles its own copy of
# sokol_gfx/sokol_gl with SOKOL_DUMMY_BACKEND (no GPU, no window, no frameworks),
# which would clash with libTrussC's platform-backend sokol implementation. It is
# therefore built with plain CMake (no trusscli), and build_all.py detects it by
# the presence of this committed CMakeLists.txt.
cmake_minimum_required(VERSION 3.16)
project(sglLayerUpload C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(sglLayerUpload main.c)

# core/include/sokol (this file lives at core/tests/sglLayerUpload/)
target_include_directories(sglLayerUpload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/sokol)

if(NOT MSVC)
    # -Wno-unused-function: the dummy-backend sokol compile leaves a couple of
    # helpers (e.g. _sgl_clamp) unreferenced; that's third-party header noise.
    target_compile_options(sglLayerUpload PRIVATE -Wall -Wextra -Wno-unused-function)
    # libm: sokol_gl's matrix helpers use sinf/cosf/sqrtf. macOS links libm
    # implicitly and MSVC pulls it from the CRT, but GNU/Linux ld needs it
    # explicitly or the standalone test fails to link (undefined references).
    target_link_libraries(sglLayerUpload PRIVATE m)
endif()
