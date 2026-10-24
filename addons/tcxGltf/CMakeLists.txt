# =============================================================================
# tcxGltf - TrussC glTF 2.0 Loader Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxGltf)

# Fetch cgltf (single-header C glTF parser)
include(FetchContent)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
)
FetchContent_MakeAvailable(cgltf)

# Keep the fetched source intact. The implementation copy guards failed parse
# cleanup/fixup and records calloc overflow; see PROVENANCE.md for both patches.
file(READ "${cgltf_SOURCE_DIR}/cgltf.h" CGLTF_SOURCE)
# A Windows git checkout may use CRLF; match and verify the LF-normalized copy.
string(REPLACE "\r\n" "\n" CGLTF_SOURCE "${CGLTF_SOURCE}")
string(SHA256 CGLTF_SHA256 "${CGLTF_SOURCE}")
if(NOT CGLTF_SHA256 STREQUAL "e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec")
    message(FATAL_ERROR "cgltf v1.15 source changed; review the allocation-failure guard")
endif()
set(CGLTF_PARSE_ROOT "int i = cgltf_parse_json_root(options, tokens, 0, json_chunk, data);")
string(FIND "${CGLTF_SOURCE}" "${CGLTF_PARSE_ROOT}" CGLTF_PARSE_OFFSET)
if(CGLTF_PARSE_OFFSET EQUAL -1)
    message(FATAL_ERROR "cgltf v1.15 parse entry changed; review the allocation-failure guard")
endif()
string(REPLACE "${CGLTF_PARSE_ROOT}"
    "${CGLTF_PARSE_ROOT}\n\tif (CGLTF_TRUSSC_CALLBACK_FAILED(options->memory.user_data))\n\t{\n\t\toptions->memory.free_func(options->memory.user_data, tokens);\n\t\treturn cgltf_result_out_of_memory;\n\t}"
    CGLTF_SOURCE "${CGLTF_SOURCE}")
set(CGLTF_CALLOC_OVERFLOW "if (SIZE_MAX / element_size < count)\n\t{\n")
string(FIND "${CGLTF_SOURCE}" "${CGLTF_CALLOC_OVERFLOW}" CGLTF_CALLOC_OFFSET)
if(CGLTF_CALLOC_OFFSET EQUAL -1)
    message(FATAL_ERROR "cgltf v1.15 calloc changed; review overflow recording")
endif()
string(REPLACE "${CGLTF_CALLOC_OVERFLOW}"
    "${CGLTF_CALLOC_OVERFLOW}\t\tCGLTF_TRUSSC_RECORD_OOM(options->memory.user_data);\n"
    CGLTF_SOURCE "${CGLTF_SOURCE}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cgltf-safe")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cgltf-safe/source.tmp" "${CGLTF_SOURCE}")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/cgltf-safe/source.tmp"
    "${CMAKE_CURRENT_BINARY_DIR}/cgltf-safe/tcxGltfCgltf.h" COPYONLY)
file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/cgltf-safe/source.tmp")

# Source files
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# cgltf is header-only; just add include path
target_include_directories(${ADDON_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/cgltf-safe
)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)

# STL allocation failures must reach the loader's recovery path on Web too.
if(EMSCRIPTEN)
    target_compile_options(${ADDON_NAME} PRIVATE -fexceptions)
    target_link_options(${ADDON_NAME} INTERFACE -fexceptions -sABORTING_MALLOC=0)
endif()
