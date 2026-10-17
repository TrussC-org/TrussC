# This sibling directory gets both the generated include path and the ordering
# through target_link_libraries alone, without a library for the producer itself.
add_executable(shaderTargetOrdering main.c)
target_include_directories(shaderTargetOrdering PRIVATE "${TRUSSC_DIR}/include/sokol")
target_link_libraries(shaderTargetOrdering PRIVATE producer_shaders empty_shaders)
if(MSVC)
    # This standalone target does not inherit TrussC's UTF-8 compile option.
    target_compile_options(shaderTargetOrdering PRIVATE /utf-8)
endif()
