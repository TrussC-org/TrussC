# GCC 15 diagnostics: separate processes, no TrussC headers or linkage.
# Keep other platforms/compilers free of the extra Box2D builds.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_target_property(_box2d_source_dir box2d SOURCE_DIR)
    get_target_property(_box2d_sources box2d SOURCES)
    list(TRANSFORM _box2d_sources PREPEND "${_box2d_source_dir}/")

    function(box2d_proxy_probe variant)
        set(probe box2d_proxy_${variant})
        set(library box2d)
        if(ARGN)
            set(library ${probe}_library)
            add_library(${library} STATIC ${_box2d_sources})
            target_include_directories(${library}
                PUBLIC "${_box2d_source_dir}/../include"
                PRIVATE "${_box2d_source_dir}")
            set_target_properties(${library} PROPERTIES
                CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
                CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
            target_compile_options(${library} PRIVATE ${ARGN})
        endif()

        # Outside src/: trussc_app() must not add this main() to the harness.
        add_executable(${probe} box2d_proxy_probe.cpp)
        target_link_libraries(${probe} PRIVATE ${library})
        target_compile_options(${probe} PRIVATE ${ARGN})
        target_compile_definitions(${probe} PRIVATE BOX2D_PROXY_VARIANT="${variant}")
        add_custom_target(run_${probe}
            COMMAND $<TARGET_FILE:${probe}>
            DEPENDS ${probe}
            VERBATIM)
        # build_all's addon-tests flow builds the harness before running it.
        # Run diagnostics even on incremental builds and before harness failures.
        add_dependencies(${PROJECT_NAME} run_${probe})
    endfunction()

    box2d_proxy_probe(as-is)
    box2d_proxy_probe(O0 -O0)
    box2d_proxy_probe(O1 -O1)
    box2d_proxy_probe(O2-no-strict-aliasing -O2 -fno-strict-aliasing)
    box2d_proxy_probe(O2-no-tree-vectorize -O2 -fno-tree-vectorize)
    box2d_proxy_probe(O2-fp-contract-off -O2 -ffp-contract=off)
endif()
