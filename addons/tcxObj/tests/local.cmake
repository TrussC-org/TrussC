# Optional real graphics run checks that material drawing selects the PBR path.
option(TCX_OBJ_TEST_GPU "Run OBJ tests in a graphics context (Xvfb on Linux)" OFF)
if(TCX_OBJ_TEST_GPU)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TCX_OBJ_TEST_GPU)
else()
    # Headless texture tests use dummy GPU resources, with no window or device.
    set(_headless_sokol "${TRUSSC_DIR}/tests/common/tcHeadlessSokol.cpp")
    target_sources(${PROJECT_NAME} PRIVATE "${_headless_sokol}")
    if(APPLE)
        set_source_files_properties("${_headless_sokol}" PROPERTIES LANGUAGE OBJCXX)
    endif()
endif()
